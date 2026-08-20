#pragma once

#include <filesystem>
#include <memory>
#include <string>

class Config {
 public:
  explicit Config(std::filesystem::path ini_path);

  const std::filesystem::path& path() const { return ini_path_; }

  std::wstring GetWString(const wchar_t* section,
                          const wchar_t* key,
                          const wchar_t* default_value = L"") const;
  std::string GetString(const wchar_t* section,
                        const wchar_t* key,
                        const char* default_value = "") const;
  int GetInt(const wchar_t* section,
             const wchar_t* key,
             int default_value) const;
  bool GetBool(const wchar_t* section,
               const wchar_t* key,
               bool default_value) const;

  std::filesystem::path GetPath(const wchar_t* section,
                                const wchar_t* key,
                                const std::filesystem::path& default_value) const;

  // 运行时开关需要写回 set.ini。WritePrivateProfileStringW 本身是线程安全的
  // Windows 配置 API；这里只包装成明确的 bool 接口。
  bool SetBool(const wchar_t* section, const wchar_t* key, bool value) const;
  bool SetWString(const wchar_t* section,
                  const wchar_t* key,
                  const std::wstring& value) const;

 private:
  std::filesystem::path ini_path_;
};
