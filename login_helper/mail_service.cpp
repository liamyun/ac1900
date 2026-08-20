#include "mail_service.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

#include <utility>
#include "config.h"
#include "market_data.h"
#include "util.h"

namespace {

std::string LowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string NormalizeNewlines(std::string s) {
  std::string out;
  out.reserve(s.size() + 64);
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r') {
      if (i + 1 < s.size() && s[i + 1] == '\n') {
        out += "\r\n";
        ++i;
      } else {
        out += "\r\n";
      }
    } else if (s[i] == '\n') {
      out += "\r\n";
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

}  // namespace

MailService::MailService(std::shared_ptr<Config> config)
    : config_(std::move(config)) {
  // 兼容旧 set.ini 的 [email] 区域。
  enabled_ = config_->GetBool(L"email", L"enabled", false);
  user_ = config_->GetString(L"email", L"user", "");
  password_ = config_->GetString(L"email", L"password", "");
  pop_host_ = config_->GetString(L"email", L"pop_host", "pop.163.com");
  pop_port_ = config_->GetInt(L"email", L"pop_port", 995);
  smtp_host_ = config_->GetString(L"email", L"smtp_host", "smtp.163.com");
  smtp_port_ = config_->GetInt(L"email", L"smtp_port", 465);
  allowed_senders_ = config_->GetString(L"email", L"all", "");
  state_file_ = config_->path().parent_path() / L"runtime" /
                L"last_mail_message_id.txt";

  if (user_.empty() || password_.empty()) {
    enabled_ = false;
  }
}

bool MailService::CurlAvailable() const {
  wchar_t path[MAX_PATH]{};
  return ::SearchPathW(nullptr, L"curl.exe", nullptr, MAX_PATH,
                       path, nullptr) > 0;
}

std::string MailService::CurlConfigQuote(const std::string& value) const {
  std::string out;
  out.reserve(value.size() + 16);
  for (char c : value) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\r' || c == '\n') {
      // 配置文件每行一个选项，不能允许凭据或地址注入新选项。
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::filesystem::path MailService::MakeTempFile(
    const wchar_t* prefix,
    const wchar_t* extension) const {
  wchar_t temp_dir[MAX_PATH]{};
  if (::GetTempPathW(MAX_PATH, temp_dir) == 0) {
    return {};
  }
  wchar_t temp_file[MAX_PATH]{};
  if (::GetTempFileNameW(temp_dir, prefix, 0, temp_file) == 0) {
    return {};
  }

  std::filesystem::path path(temp_file);
  if (extension && *extension) {
    std::filesystem::path renamed = path;
    renamed.replace_extension(extension);
    if (::MoveFileExW(path.c_str(), renamed.c_str(),
                      MOVEFILE_REPLACE_EXISTING)) {
      path = renamed;
    }
  }
  return path;
}

bool MailService::RunCurlConfig(const std::string& config_text,
                                std::string& output,
                                DWORD timeout_ms) const {
  output.clear();
  if (!CurlAvailable()) {
    return false;
  }

  const auto cfg_path = MakeTempFile(L"pcf", L".cfg");
  if (cfg_path.empty() || !util::WriteTextFile(cfg_path, config_text)) {
    return false;
  }

  wchar_t curl_path[MAX_PATH]{};
  if (::SearchPathW(nullptr, L"curl.exe", nullptr, MAX_PATH,
                    curl_path, nullptr) == 0) {
    ::DeleteFileW(cfg_path.c_str());
    return false;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = static_cast<DWORD>(sizeof(sa));
  sa.bInheritHandle = TRUE;
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!::CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    ::DeleteFileW(cfg_path.c_str());
    return false;
  }
  ::SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = static_cast<DWORD>(sizeof(si));
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  std::wstring cmd = util::QuoteCommandLineArg(curl_path);
  cmd += L" --config ";
  cmd += util::QuoteCommandLineArg(cfg_path.wstring());

  std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
  mutable_cmd.push_back(L'\0');

  const BOOL created = ::CreateProcessW(
      nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

  ::CloseHandle(write_pipe);
  write_pipe = nullptr;

  bool ok = false;
  if (created) {
    const ULONGLONG deadline = ::GetTickCount64() + timeout_ms;
    char buffer[8192];
    while (true) {
      DWORD available = 0;
      if (!::PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) {
        break;
      }
      if (available > 0) {
        DWORD read = 0;
        const DWORD wanted =
            std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        if (::ReadFile(read_pipe, buffer, wanted, &read, nullptr) && read > 0) {
          output.append(buffer, buffer + read);
        }
        continue;
      }

      const DWORD wait = ::WaitForSingleObject(pi.hProcess, 20);
      if (wait == WAIT_OBJECT_0) {
        // 把进程退出前留在管道里的最后一点数据读干净。
        while (true) {
          DWORD read = 0;
          if (!::ReadFile(read_pipe, buffer, static_cast<DWORD>(sizeof(buffer)),
                          &read, nullptr) ||
              read == 0) {
            break;
          }
          output.append(buffer, buffer + read);
        }
        DWORD exit_code = 1;
        ::GetExitCodeProcess(pi.hProcess, &exit_code);
        ok = exit_code == 0;
        break;
      }

      if (::GetTickCount64() >= deadline) {
        ::TerminateProcess(pi.hProcess, 2);
        ::WaitForSingleObject(pi.hProcess, 5000);
        break;
      }
    }

    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
  }

  ::CloseHandle(read_pipe);
  ::DeleteFileW(cfg_path.c_str());
  return created && ok;
}

bool MailService::FetchLatestRawMessage(std::string& raw_message) {
  raw_message.clear();

  std::ostringstream list_cfg;
  list_cfg << "silent\nshow-error\nssl-reqd\n"
           << "connect-timeout = 15\nmax-time = 45\n"
           << "url = \"pop3s://" << CurlConfigQuote(pop_host_) << ":"
           << pop_port_ << "/\"\n"
           << "user = \"" << CurlConfigQuote(user_ + ":" + password_) << "\"\n";

  std::string listing;
  if (!RunCurlConfig(list_cfg.str(), listing)) {
    return false;
  }

  int latest_id = -1;
  std::istringstream lines(listing);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream one(line);
    int id = -1;
    long long size = 0;
    if (one >> id >> size) {
      latest_id = std::max(latest_id, id);
    }
  }
  if (latest_id < 0) {
    return false;
  }

  std::ostringstream get_cfg;
  get_cfg << "silent\nshow-error\nssl-reqd\n"
          << "connect-timeout = 15\nmax-time = 45\n"
          << "url = \"pop3s://" << CurlConfigQuote(pop_host_) << ":"
          << pop_port_ << "/" << latest_id << "\"\n"
          << "user = \"" << CurlConfigQuote(user_ + ":" + password_) << "\"\n";

  return RunCurlConfig(get_cfg.str(), raw_message);
}

std::string MailService::HeaderValue(const std::string& raw_message,
                                     const std::string& header) const {
  const std::string wanted = LowerAscii(header);
  std::istringstream in(raw_message);
  std::string line;
  std::string value;
  bool collecting = false;

  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;

    if ((line.front() == ' ' || line.front() == '\t') && collecting) {
      value += " " + util::Trim(line);
      continue;
    }

    collecting = false;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    if (LowerAscii(util::Trim(line.substr(0, colon))) == wanted) {
      value = util::Trim(line.substr(colon + 1));
      collecting = true;
    }
  }
  return value;
}

std::string MailService::ExtractAddress(
    const std::string& from_header) const {
  const size_t left = from_header.find('<');
  const size_t right = from_header.find('>', left == std::string::npos ? 0 : left);
  if (left != std::string::npos && right != std::string::npos &&
      right > left + 1) {
    return util::Trim(from_header.substr(left + 1, right - left - 1));
  }

  // 没有尖括号时取最后一个含 @ 的 token。
  std::string cleaned = from_header;
  for (char& c : cleaned) {
    if (c == ',' || c == ';' || c == '"' || c == '(' || c == ')') c = ' ';
  }
  std::istringstream in(cleaned);
  std::string token;
  std::string address;
  while (in >> token) {
    if (token.find('@') != std::string::npos) {
      address = token;
    }
  }
  return util::Trim(address);
}

bool MailService::SenderAllowed(const std::string& sender) const {
  if (sender.empty() || allowed_senders_.empty()) {
    return false;
  }
  // 旧程序用 all.find(sender) 判断；这里保持相同兼容语义，同时忽略大小写。
  return LowerAscii(allowed_senders_).find(LowerAscii(sender)) !=
         std::string::npos;
}

bool MailService::PollTrigger(std::string& sender) {
  sender.clear();
  if (!enabled_) {
    return false;
  }

  std::string raw;
  if (!FetchLatestRawMessage(raw)) {
    return false;
  }

  std::string message_id = HeaderValue(raw, "message-id");
  if (message_id.empty()) {
    // 少数邮件无 Message-ID，用头部和长度做一个稳定指纹。
    std::hash<std::string> h;
    message_id = "fallback-" + std::to_string(h(raw.substr(0, 4096))) +
                 "-" + std::to_string(raw.size());
  }

  const std::string last = util::Trim(util::ReadTextFile(state_file_));
  if (last == message_id) {
    return false;
  }

  // 无论是否在白名单，都标记为已见，避免每分钟反复处理同一封陌生邮件。
  util::WriteTextFile(state_file_, message_id);

  const std::string from = ExtractAddress(HeaderValue(raw, "from"));
  if (!SenderAllowed(from)) {
    return false;
  }

  sender = from;
  return true;
}

std::string MailService::WrapBase64(const std::string& base64) const {
  std::string out;
  for (size_t i = 0; i < base64.size(); i += 76) {
    out.append(base64, i, std::min<size_t>(76, base64.size() - i));
    out += "\r\n";
  }
  return out;
}

std::string MailService::BuildMime(const std::string& recipient,
                                   const ReportFiles& report) const {
  const std::string boundary =
      "----PersonalCEF-" + util::LocalDateTimeCompact();

  const std::string subject_utf8 =
      "铜铝分时数 " + util::LocalTimestamp();
  const std::string subject =
      "=?UTF-8?B?" + util::Base64Encode(subject_utf8) + "?=";

  std::ostringstream mime;
  mime << "From: <" << user_ << ">\r\n"
       << "To: <" << recipient << ">\r\n"
       << "Subject: " << subject << "\r\n"
       << "MIME-Version: 1.0\r\n"
       << "Content-Type: multipart/mixed; boundary=\"" << boundary
       << "\"\r\n\r\n";

  mime << "--" << boundary << "\r\n"
       << "Content-Type: text/plain; charset=utf-8\r\n"
       << "Content-Transfer-Encoding: base64\r\n\r\n"
       << WrapBase64(util::Base64Encode(
              NormalizeNewlines(report.mail_body)))
       << "\r\n";

  for (const auto& path : report.Attachments()) {
    const std::string bytes = util::ReadTextFile(path);
    const std::string filename = util::WideToUtf8(path.filename().wstring());
    mime << "--" << boundary << "\r\n"
         << "Content-Type: text/csv; name=\"" << filename << "\"\r\n"
         << "Content-Transfer-Encoding: base64\r\n"
         << "Content-Disposition: attachment; filename=\"" << filename
         << "\"\r\n\r\n"
         << WrapBase64(util::Base64Encode(bytes))
         << "\r\n";
  }

  mime << "--" << boundary << "--\r\n";
  return mime.str();
}

bool MailService::SendReport(const std::string& recipient,
                             const ReportFiles& report) {
  if (!enabled_ || recipient.empty() || !report.ok) {
    return false;
  }

  const auto mime_path = MakeTempFile(L"pcm", L".eml");
  if (mime_path.empty()) {
    return false;
  }
  const std::string mime = BuildMime(recipient, report);
  if (!util::WriteTextFile(mime_path, mime)) {
    return false;
  }

  std::ostringstream cfg;
  cfg << "silent\nshow-error\nssl-reqd\n"
      << "connect-timeout = 15\nmax-time = 90\n"
      << "url = \"smtps://" << CurlConfigQuote(smtp_host_) << ":"
      << smtp_port_ << "\"\n"
      << "user = \"" << CurlConfigQuote(user_ + ":" + password_) << "\"\n"
      << "mail-from = \"" << CurlConfigQuote(user_) << "\"\n"
      << "mail-rcpt = \"" << CurlConfigQuote(recipient) << "\"\n"
      << "upload-file = \""
      << CurlConfigQuote(util::WideToUtf8(mime_path.wstring())) << "\"\n";

  std::string output;
  const bool ok = RunCurlConfig(cfg.str(), output, 100000);
  ::DeleteFileW(mime_path.c_str());
  return ok;
}
