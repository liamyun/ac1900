#include "personal_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>
#include <iomanip>

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_helpers.h"
#include "tests/cefclient/browser/main_context.h"
#include "tests/cefclient/browser/root_window_manager.h"
#include "tests/shared/browser/main_message_loop.h"

#include "config.h"
#include "mail_service.h"
#include "market_data.h"
#include "network_capture.h"
#include "scheduler.h"
#include "util.h"

namespace client::personal {
namespace {

class PassThroughCaptureFilter final : public CefResponseFilter {
 public:
  explicit PassThroughCaptureFilter(
      std::shared_ptr<Runtime::ResponseState> state)
      : state_(std::move(state)) {}

  bool InitFilter() override { return state_ != nullptr; }

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override {
    data_in_read = 0;
    data_out_written = 0;

    if (!data_out || data_out_size == 0) {
      return RESPONSE_FILTER_NEED_MORE_DATA;
    }
    if (!data_in || data_in_size == 0) {
      return RESPONSE_FILTER_DONE;
    }

    // CEF 的 ResponseFilter 是“流式块”语义，不是 TCP 粘包语义。
    // 只报告实际转发出去的字节，绝不能把未复制的数据标成已消费。
    const size_t n = std::min(data_in_size, data_out_size);
    std::memcpy(data_out, data_in, n);
    data_in_read = n;
    data_out_written = n;

    if (state_->body.size() < state_->limit) {
      const size_t remaining = state_->limit - state_->body.size();
      const size_t append = std::min(n, remaining);
      state_->body.append(static_cast<const char*>(data_in), append);
      if (append < n) {
        state_->truncated = true;
      }
    } else {
      state_->truncated = true;
    }

    return n == data_in_size ? RESPONSE_FILTER_DONE
                             : RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  std::shared_ptr<Runtime::ResponseState> state_;
  IMPLEMENT_REFCOUNTING(PassThroughCaptureFilter);
};

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool HostMatches(const std::string& host, const char* domain) {
  const std::string d(domain);
  if (host == d) {
    return true;
  }
  return host.size() > d.size() &&
         host.compare(host.size() - d.size(), d.size(), d) == 0 &&
         host[host.size() - d.size() - 1] == '.';
}

bool CsvHasToken(const std::string& csv, const std::string& token) {
  size_t start = 0;
  while (start <= csv.size()) {
    const size_t end = csv.find(',', start);
    const size_t count =
        end == std::string::npos ? std::string::npos : end - start;
    if (csv.substr(start, count) == token) {
      return true;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

void ReplaceSwitch(CefRefPtr<CefCommandLine> command_line,
                   const char* name,
                   const std::string& value) {
  if (!command_line || !name || !*name) {
    return;
  }
  if (command_line->HasSwitch(name)) {
    command_line->RemoveSwitch(name);
  }
  if (value.empty()) {
    command_line->AppendSwitch(name);
  } else {
    command_line->AppendSwitchWithValue(name, value);
  }
}


std::filesystem::path NetworkDiagnosticPath() {
  return util::ExeDir() / L"runtime" / L"logs" / L"network-diagnostic.log";
}

std::string SanitizeDiagnosticUrl(std::string url) {
  const size_t cut = url.find_first_of("?#");
  if (cut != std::string::npos) {
    url.resize(cut);
  }
  return url;
}

std::string ExtractUrlHost(const std::string& raw_url) {
  const std::string url = LowerAscii(raw_url);
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) {
    return {};
  }
  const size_t begin = scheme + 3;
  const size_t end = url.find_first_of("/?#", begin);
  std::string authority = url.substr(
      begin, end == std::string::npos ? std::string::npos : end - begin);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority.erase(0, at + 1);
  }
  if (!authority.empty() && authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close != std::string::npos) {
      return authority.substr(1, close - 1);
    }
  }
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    authority.resize(colon);
  }
  return authority;
}

bool IsNetworkDiagnosticUrl(const std::string& raw_url) {
  const std::string host = ExtractUrlHost(raw_url);
  if (host.empty()) {
    return false;
  }
  return HostMatches(host, "chatgpt.com") ||
         HostMatches(host, "openai.com") ||
         HostMatches(host, "oaistatic.com") ||
         HostMatches(host, "oaiusercontent.com") ||
         HostMatches(host, "stripe.com") ||
         HostMatches(host, "stripe.network");
}

void AppendNetworkDiagnostic(const std::string& message) {
  static std::mutex file_mutex;
  std::lock_guard lock(file_mutex);
  std::ostringstream line;
  line << '[' << util::LocalTimestamp() << "] " << message << "\n";
  util::AppendTextFile(NetworkDiagnosticPath(), line.str());
}


std::string TransitionToString(cef_transition_type_t transition) {
  const int value = static_cast<int>(transition);
  const int source = value & TT_SOURCE_MASK;
  const char* source_name = "UNKNOWN";
  switch (source) {
    case TT_LINK: source_name = "LINK"; break;
    case TT_EXPLICIT: source_name = "EXPLICIT"; break;
    case TT_AUTO_BOOKMARK: source_name = "AUTO_BOOKMARK"; break;
    case TT_AUTO_SUBFRAME: source_name = "AUTO_SUBFRAME"; break;
    case TT_MANUAL_SUBFRAME: source_name = "MANUAL_SUBFRAME"; break;
    case TT_GENERATED: source_name = "GENERATED"; break;
    case TT_AUTO_TOPLEVEL: source_name = "AUTO_TOPLEVEL"; break;
    case TT_FORM_SUBMIT: source_name = "FORM_SUBMIT"; break;
    case TT_RELOAD: source_name = "RELOAD"; break;
    case TT_KEYWORD: source_name = "KEYWORD"; break;
    case TT_KEYWORD_GENERATED: source_name = "KEYWORD_GENERATED"; break;
  }
  std::ostringstream out;
  out << source_name;
  if (value & TT_FORWARD_BACK_FLAG) out << "+FORWARD_BACK";
  if (value & TT_DIRECT_LOAD_FLAG) out << "+DIRECT_LOAD";
  if (value & TT_HOME_PAGE_FLAG) out << "+HOME_PAGE";
  if (value & TT_FROM_API_FLAG) out << "+FROM_API";
  if (value & TT_CHAIN_START_FLAG) out << "+CHAIN_START";
  if (value & TT_CHAIN_END_FLAG) out << "+CHAIN_END";
  if (value & TT_CLIENT_REDIRECT_FLAG) out << "+CLIENT_REDIRECT";
  if (value & TT_SERVER_REDIRECT_FLAG) out << "+SERVER_REDIRECT";
  out << "(0x" << std::hex << std::uppercase << static_cast<unsigned int>(value)
      << std::dec << ")";
  return out.str();
}

std::string HeaderSummary(CefRefPtr<CefRequest> request) {
  if (!request) return {};
  const std::string cookie = request->GetHeaderByName("Cookie").ToString();
  const std::string auth = request->GetHeaderByName("Authorization").ToString();
  const std::string origin = request->GetHeaderByName("Origin").ToString();
  const std::string fetch_site = request->GetHeaderByName("Sec-Fetch-Site").ToString();
  const std::string fetch_mode = request->GetHeaderByName("Sec-Fetch-Mode").ToString();
  const std::string fetch_dest = request->GetHeaderByName("Sec-Fetch-Dest").ToString();
  std::ostringstream out;
  out << " cookie=" << (cookie.empty() ? "no" : ("yes(" + std::to_string(cookie.size()) + ")"))
      << " auth=" << (auth.empty() ? "no" : "yes")
      << " origin=" << (origin.empty() ? "<none>" : SanitizeDiagnosticUrl(origin))
      << " fetch_site=" << (fetch_site.empty() ? "<none>" : fetch_site)
      << " fetch_mode=" << (fetch_mode.empty() ? "<none>" : fetch_mode)
      << " fetch_dest=" << (fetch_dest.empty() ? "<none>" : fetch_dest);
  return out.str();
}

std::string ResponseHeaderSummary(CefRefPtr<CefResponse> response) {
  if (!response) return {};
  auto h = [&](const char* name) { return response->GetHeaderByName(name).ToString(); };
  const std::string server = h("Server");
  const std::string cf_ray = h("CF-Ray");
  const std::string cf_mitigated = h("cf-mitigated");
  const std::string content_type = h("Content-Type");
  const std::string request_id = h("x-request-id");
  const std::string set_cookie = h("Set-Cookie");
  std::ostringstream out;
  out << " mime=" << response->GetMimeType().ToString()
      << " server=" << (server.empty() ? "<none>" : server)
      << " cf_ray=" << (cf_ray.empty() ? "<none>" : cf_ray)
      << " cf_mitigated=" << (cf_mitigated.empty() ? "<none>" : cf_mitigated)
      << " content_type=" << (content_type.empty() ? "<none>" : content_type)
      << " x_request_id=" << (request_id.empty() ? "<none>" : request_id)
      << " set_cookie=" << (set_cookie.empty() ? "no" : ("yes(" + std::to_string(set_cookie.size()) + ")"));
  return out.str();
}

std::string EffectiveSwitch(CefRefPtr<CefCommandLine> command_line,
                            const char* name) {
  if (!command_line || !command_line->HasSwitch(name)) {
    return "<absent>";
  }
  const std::string value = command_line->GetSwitchValue(name).ToString();
  return value.empty() ? "<present>" : value;
}

void AddDisabledFeature(CefRefPtr<CefCommandLine> command_line,
                        const std::string& feature) {
  if (!command_line || feature.empty()) {
    return;
  }

  std::string disabled =
      command_line->GetSwitchValue("disable-features").ToString();
  if (CsvHasToken(disabled, feature)) {
    return;
  }
  if (!disabled.empty()) {
