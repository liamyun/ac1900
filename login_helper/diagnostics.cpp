#include "diagnostics.h"

#include <windows.h>
#include <dbghelp.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include "util.h"

namespace diag {
namespace {

std::mutex g_log_mutex;
std::filesystem::path g_log_path;
std::filesystem::path g_dump_dir;
std::array<wchar_t, 32768> g_dump_dir_raw{};

std::string Timestamp() {
  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  char buf[64]{};
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);
  return buf;
}

void AppendRawLine(std::string_view line) {
  if (g_log_path.empty()) return;
  HANDLE file = ::CreateFileW(
      g_log_path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  ::WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
              nullptr);
  ::CloseHandle(file);
}

LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* info) {
  const DWORD pid = ::GetCurrentProcessId();
  const DWORD tid = ::GetCurrentThreadId();
  const DWORD code = (info && info->ExceptionRecord)
                         ? info->ExceptionRecord->ExceptionCode
                         : 0;
  void* address = (info && info->ExceptionRecord)
                      ? info->ExceptionRecord->ExceptionAddress
                      : nullptr;

  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  char ts[64]{};
  std::snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);

  char line[512]{};
  std::snprintf(line, sizeof(line),
                "%s | pid=%lu tid=%lu | process.unhandled_exception | "
                "code=0x%08lX address=%p\r\n",
                ts, static_cast<unsigned long>(pid),
                static_cast<unsigned long>(tid),
                static_cast<unsigned long>(code), address);
  AppendRawLine(line);

  if (g_dump_dir_raw[0] != L'\0') {
    wchar_t dump_path[32768]{};
    _snwprintf_s(
        dump_path, _countof(dump_path), _TRUNCATE,
        L"%s\\crash-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu-0x%08lX.dmp",
        g_dump_dir_raw.data(), st.wYear, st.wMonth, st.wDay, st.wHour,
        st.wMinute, st.wSecond, static_cast<unsigned long>(pid),
        static_cast<unsigned long>(tid), static_cast<unsigned long>(code));

    HANDLE dump = ::CreateFileW(dump_path, GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION exception_info{};
      exception_info.ThreadId = tid;
      exception_info.ExceptionPointers = info;
      exception_info.ClientPointers = FALSE;
      const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
          MiniDumpNormal | MiniDumpWithThreadInfo |
          MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
      ::MiniDumpWriteDump(::GetCurrentProcess(), pid, dump, type,
                          info ? &exception_info : nullptr, nullptr, nullptr);
      ::CloseHandle(dump);
    }
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void Initialize(const std::filesystem::path& runtime_dir) {
  std::lock_guard lock(g_log_mutex);
  g_dump_dir = runtime_dir / L"diagnostics";
  g_log_path = g_dump_dir / L"diagnostic.log";
  util::EnsureDir(g_dump_dir);

  const std::wstring dump_dir = g_dump_dir.wstring();
  wcsncpy_s(g_dump_dir_raw.data(), g_dump_dir_raw.size(), dump_dir.c_str(),
            _TRUNCATE);

  ::SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);

  std::string line = Timestamp() + " | pid=" +
                     std::to_string(::GetCurrentProcessId()) + " tid=" +
                     std::to_string(::GetCurrentThreadId()) +
                     " | process.start | cmd=" +
                     util::WideToUtf8(::GetCommandLineW()) + "\r\n";
  AppendRawLine(line);
}

void ReinstallCrashHandler() {
  ::SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);
  Log("diagnostic.crash_handler_reinstalled");
}

void Log(std::string_view event, std::string_view detail) {
  std::lock_guard lock(g_log_mutex);
  std::string line;
  line.reserve(event.size() + detail.size() + 128);
  line += Timestamp();
  line += " | pid=";
  line += std::to_string(::GetCurrentProcessId());
  line += " tid=";
  line += std::to_string(::GetCurrentThreadId());
  line += " | ";
  line.append(event);
  if (!detail.empty()) {
    line += " | ";
    line.append(detail);
  }
  line += "\r\n";
  AppendRawLine(line);
}

void LogWide(std::string_view event, std::wstring_view detail) {
  Log(event, util::WideToUtf8(detail));
}

std::filesystem::path LogPath() { return g_log_path; }
std::filesystem::path DumpDirectory() { return g_dump_dir; }

}  // namespace diag
