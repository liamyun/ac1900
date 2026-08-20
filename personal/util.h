#pragma once

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace util {

std::filesystem::path ExeDir();
std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);
std::string Trim(std::string text);
std::wstring Trim(std::wstring text);

bool EnsureDir(const std::filesystem::path& dir);
bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);
bool WriteTextFile(const std::filesystem::path& path, std::string_view text);
bool AppendTextFile(const std::filesystem::path& path, std::string_view text);
std::string ReadTextFile(const std::filesystem::path& path);

std::string JsonEscape(std::string_view s);
std::string HtmlEscape(std::string_view s);
bool IsValidUtf8(std::string_view s);
std::string Base64Encode(std::string_view data);
std::string Base64Encode(const std::vector<uint8_t>& data);

std::string LocalTimestamp();
std::string LocalDate();
std::string LocalDateTimeCompact();

bool ParseHHMM(std::string_view value, int& hour, int& minute);
bool IsSameHHMM(std::string_view value, const SYSTEMTIME& st);

std::string ShellSafeName(std::string s);
std::wstring QuoteCommandLineArg(const std::wstring& arg);

}  // namespace util
