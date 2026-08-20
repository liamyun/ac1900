#include "util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace util {

std::filesystem::path ExeDir() {
  std::wstring buffer(32768, L'\0');
  const DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(),
                                        static_cast<DWORD>(buffer.size()));
  if (len == 0 || static_cast<size_t>(len) >= buffer.size()) {
    return std::filesystem::current_path();
  }
  buffer.resize(len);
  return std::filesystem::path(buffer).parent_path();
}

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
  if (len <= 0) {
    // 网络里偶尔可能遇到非严格 UTF-8；退回宽松转换，避免采集线程崩掉。
    const int fallback_len = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (fallback_len <= 0) {
      return {};
    }
    std::wstring result(fallback_len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                          static_cast<int>(text.size()),
                          result.data(), fallback_len);
    return result;
  }
  std::wstring result(len, L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), len);
  return result;
}

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const int len = ::WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
      nullptr, nullptr);
  if (len <= 0) {
    return {};
  }
  std::string result(len, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), len,
                        nullptr, nullptr);
  return result;
}

std::string Trim(std::string text) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
             text.end());
  return text;
}

std::wstring Trim(std::wstring text) {
  const auto not_space = [](wchar_t c) { return !iswspace(c); };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
             text.end());
  return text;
}

bool EnsureDir(const std::filesystem::path& dir) {
  if (dir.empty()) {
    return true;
  }
  std::error_code ec;
  if (std::filesystem::exists(dir, ec)) {
    return !ec;
  }
  return std::filesystem::create_directories(dir, ec) && !ec;
}

bool WriteBinaryFile(const std::filesystem::path& path,
                     const void* data,
                     size_t size) {
  EnsureDir(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  if (size) {
    out.write(static_cast<const char*>(data),
              static_cast<std::streamsize>(size));
  }
  return static_cast<bool>(out);
}

bool WriteTextFile(const std::filesystem::path& path, std::string_view text) {
  return WriteBinaryFile(path, text.data(), text.size());
}

bool AppendTextFile(const std::filesystem::path& path, std::string_view text) {
  EnsureDir(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) {
    return false;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(out);
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::string JsonEscape(std::string_view s) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u00" << std::setw(2) << static_cast<int>(c);
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

std::string HtmlEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 32);
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

bool IsValidUtf8(std::string_view s) {
  if (s.empty()) {
    return true;
  }
  if (s.size() > static_cast<size_t>(INT_MAX)) {
    return false;
  }
  return ::MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
             static_cast<int>(s.size()), nullptr, 0) > 0;
}

static std::string Base64EncodeBytes(const uint8_t* data, size_t size) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t a = data[i];
    const uint32_t b = (i + 1 < size) ? data[i + 1] : 0;
    const uint32_t c = (i + 2 < size) ? data[i + 2] : 0;
    const uint32_t n = (a << 16) | (b << 8) | c;
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out.push_back(i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < size ? kAlphabet[n & 63] : '=');
  }
  return out;
}

std::string Base64Encode(std::string_view data) {
  return Base64EncodeBytes(
      reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string Base64Encode(const std::vector<uint8_t>& data) {
  return Base64EncodeBytes(data.data(), data.size());
}

static SYSTEMTIME GetLocalSystemTime() {
  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  return st;
}

std::string LocalTimestamp() {
  const auto st = GetLocalSystemTime();
  char buf[64]{};
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);
  return buf;
}

std::string LocalDate() {
  const auto st = GetLocalSystemTime();
  char buf[32]{};
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u",
                st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::string LocalDateTimeCompact() {
  const auto st = GetLocalSystemTime();
  char buf[32]{};
  std::snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
  return buf;
}

bool ParseHHMM(std::string_view value, int& hour, int& minute) {
  hour = minute = -1;
  std::string s(value);
  if (std::sscanf(s.c_str(), "%d:%d", &hour, &minute) != 2) {
    return false;
  }
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

bool IsSameHHMM(std::string_view value, const SYSTEMTIME& st) {
  int hour = 0;
  int minute = 0;
  return ParseHHMM(value, hour, minute) &&
         hour == st.wHour && minute == st.wMinute;
}

std::string ShellSafeName(std::string s) {
  for (char& c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
        c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
      c = '_';
    }
  }
  return s;
}

std::wstring QuoteCommandLineArg(const std::wstring& arg) {
  if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return arg;
  }
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

}  // namespace util
