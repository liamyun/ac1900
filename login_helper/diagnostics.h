#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace diag {

// 初始化诊断日志和未处理异常转储。应尽量早调用，CEF 子进程也会执行。
void Initialize(const std::filesystem::path& runtime_dir);

// CEF 初始化之后再安装一次，尽量避免被后续库覆盖异常过滤器。
void ReinstallCrashHandler();

// 线程安全的轻量日志。不要在高频网络 IO 回调里逐包调用。
void Log(std::string_view event, std::string_view detail = {});
void LogWide(std::string_view event, std::wstring_view detail);

std::filesystem::path LogPath();
std::filesystem::path DumpDirectory();

}  // namespace diag
