#include "config.h"

#include <windows.h>

#include <utility>
#include "util.h"

Config::Config(std::filesystem::path ini_path)
    : ini_path_(std::move(ini_path)) {}

std::wstring Config::GetWString(const wchar_t* section,
                                const wchar_t* key,
                                const wchar_t* default_value) const {
  std::wstring buffer(32768, L'\0');
  const DWORD n = ::GetPrivateProfileStringW(
      section, key, default_value, buffer.data(),
      static_cast<DWORD>(buffer.size()), ini_path_.c_str());
  buffer.resize(n);
  return util::Trim(std::move(buffer));
}

std::string Config::GetString(const wchar_t* section,
                              const wchar_t* key,
                              const char* default_value) const {
  return util::WideToUtf8(
      GetWString(section, key, util::Utf8ToWide(default_value).c_str()));
}

int Config::GetInt(const wchar_t* section,
                   const wchar_t* key,
                   int default_value) const {
  return static_cast<int>(::GetPrivateProfileIntW(
      section, key, default_value, ini_path_.c_str()));
}

bool Config::GetBool(const wchar_t* section,
                     const wchar_t* key,
                     bool default_value) const {
  const auto s = GetWString(section, key, default_value ? L"1" : L"0");
  if (_wcsicmp(s.c_str(), L"true") == 0 ||
      _wcsicmp(s.c_str(), L"yes") == 0 ||
      _wcsicmp(s.c_str(), L"on") == 0) {
    return true;
  }
  if (_wcsicmp(s.c_str(), L"false") == 0 ||
      _wcsicmp(s.c_str(), L"no") == 0 ||
      _wcsicmp(s.c_str(), L"off") == 0) {
    return false;
  }
  return _wtoi(s.c_str()) != 0;
}

std::filesystem::path Config::GetPath(
    const wchar_t* section,
    const wchar_t* key,
    const std::filesystem::path& default_value) const {
  const auto value = GetWString(section, key, default_value.c_str());
  std::filesystem::path result(value);
  if (result.is_relative()) {
    result = ini_path_.parent_path() / result;
  }
  return result.lexically_normal();
}

bool Config::SetBool(const wchar_t* section,
                     const wchar_t* key,
                     bool value) const {
  return SetWString(section, key, value ? L"1" : L"0");
}

bool Config::SetWString(const wchar_t* section,
                        const wchar_t* key,
                        const std::wstring& value) const {
  const BOOL ok = ::WritePrivateProfileStringW(
      section, key, value.c_str(), ini_path_.c_str());
  // 显式刷新 profile 缓存，避免程序崩溃时最后一次开关状态没落盘。
  ::WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini_path_.c_str());
  return ok == TRUE;
}
