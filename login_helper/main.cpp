#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_library_loader.h"

#include "config.h"
#include "main_window.h"
#include "ui_messages.h"
#include "util.h"

namespace {

constexpr wchar_t kSingleInstanceMutex[] =
    L"Local\\PersonalCEF151.LoginHelper.R6";
constexpr wchar_t kMainWindowClass[] = L"PersonalCEF151.LoginHelperWindow";
constexpr int kConfigVersion = 6;

void EnableBestDpiAwareness() {
  using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
  if (!user32) return;
  auto fn = reinterpret_cast<SetDpiContextFn>(
      ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
  if (fn) {
    fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
}

void SetChatGptTaskbarIdentity() {
  // r6 使用新的 AppUserModelID，避免 Windows 继续沿用之前白色默认图标的缓存。
  // 任务栏实际图标同时由顶层窗口 WM_SETICON 显式指定。
  using SetAppIdFn = HRESULT(WINAPI*)(PCWSTR);
  HMODULE shell32 = ::GetModuleHandleW(L"shell32.dll");
  if (!shell32) return;
  auto fn = reinterpret_cast<SetAppIdFn>(
      ::GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID"));
  if (fn) {
    fn(L"PersonalCEF151.ChatGPT.LoginR6");
  }
}

void UpgradeConfig(std::shared_ptr<Config> config) {
  if (!config) {
    return;
  }
  const int old_version = config->GetInt(L"app", L"config_version", 0);
  if (old_version >= kConfigVersion) {
    return;
  }

  // 首次从更老工程迁移到 ChatGPT 专用模式时，建立 r4 的基础默认值。
  if (old_version < 4) {
    config->SetWString(L"browser", L"home_url", L"https://chatgpt.com/");
    config->SetBool(L"capture", L"enabled", false);
    config->SetBool(L"schedule", L"enabled", false);

    config->SetBool(L"proxy", L"enabled", true);
    config->SetWString(L"proxy", L"type", L"socks5");
    config->SetWString(L"proxy", L"host", L"127.0.0.1");
    config->SetWString(L"proxy", L"port", L"1080");
    config->SetWString(L"proxy", L"bypass_list",
                       L"<local>;localhost;127.0.0.1");
    config->SetBool(L"proxy", L"disable_quic", true);
  }

  // r6 只添加安全采集和皮肤字段，不重写已经存在的代理、邮件、行情等配置。
  if (old_version < 6) {
    config->SetBool(L"capture", L"enabled", false);
    config->SetBool(L"capture", L"api_only", true);
    config->SetWString(L"capture", L"max_queue_mb", L"32");
    config->SetWString(L"ui", L"theme", L"system");
  }

  config->SetWString(L"app", L"config_version",
                     std::to_wstring(kConfigVersion));
}

class PersonalCefApp final : public CefApp {
 public:
  explicit PersonalCefApp(std::shared_ptr<Config> config)
      : config_(std::move(config)) {}

  void OnBeforeCommandLineProcessing(
      const CefString&,
      CefRefPtr<CefCommandLine> command_line) override {
    if (!command_line || !config_ ||
        !config_->GetBool(L"proxy", L"enabled", false)) {
      return;
    }

    // 如果启动命令自己传了 --proxy-server，则显式命令行优先于 set.ini。
    if (command_line->HasSwitch("proxy-server")) {
      return;
    }

    std::string type = config_->GetString(L"proxy", L"type", "socks5");
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (type != "socks5" && type != "socks4" &&
        type != "http" && type != "https") {
      type = "socks5";
    }

    const std::string host = config_->GetString(L"proxy", L"host", "");
    const int port = config_->GetInt(L"proxy", L"port", 0);
    if (host.empty() || port <= 0 || port > 65535) {
      return;
    }

    command_line->AppendSwitchWithValue(
        "proxy-server", type + "://" + host + ":" + std::to_string(port));

    const std::string bypass =
        config_->GetString(L"proxy", L"bypass_list", "");
    if (!bypass.empty()) {
      command_line->AppendSwitchWithValue("proxy-bypass-list", bypass);
    }

    // SOCKS5 不承载 Chromium 的 UDP/QUIC。关闭 QUIC 后，ChatGPT 的网络连接
    // 更稳定地走 TCP + SOCKS5，避免出现部分请求绕开代理的情况。
    if (config_->GetBool(L"proxy", L"disable_quic", true)) {
      command_line->AppendSwitch("disable-quic");
    }
  }

 private:
  std::shared_ptr<Config> config_;

  IMPLEMENT_REFCOUNTING(PersonalCefApp);
  DISALLOW_COPY_AND_ASSIGN(PersonalCefApp);
};

struct HelperArgs {
  DWORD wait_pid = 0;
  bool return_main = false;
};

HelperArgs ParseHelperArgs() {
  HelperArgs result;
  int argc = 0;
  LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argv) return result;
  for (int i = 1; i < argc; ++i) {
    std::wstring arg(argv[i]);
    constexpr wchar_t kWait[] = L"--wait-pid=";
    if (arg.rfind(kWait, 0) == 0) {
      result.wait_pid = static_cast<DWORD>(_wtoi(arg.c_str() + wcslen(kWait)));
    } else if (arg == L"--return-main") {
      result.return_main = true;
    }
  }
  ::LocalFree(argv);
  return result;
}

void WaitForMainProcess(DWORD pid) {
  if (!pid) return;
  HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!process) return;
  ::WaitForSingleObject(process, 30000);
  ::CloseHandle(process);
  ::Sleep(350);
}

void RestartMainApp() {
  const auto main_exe = util::ExeDir() / L"PersonalCEF.exe";
  if (std::filesystem::exists(main_exe)) {
    ::ShellExecuteW(nullptr, L"open", main_exe.c_str(), nullptr,
                    util::ExeDir().c_str(), SW_SHOWNORMAL);
  }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance,
                      HINSTANCE,
                      wchar_t*,
                      int) {
  const bool is_subprocess = wcsstr(::GetCommandLineW(), L"--type=") != nullptr;
  const HelperArgs helper_args = is_subprocess ? HelperArgs{} : ParseHelperArgs();
  if (!is_subprocess) {
    WaitForMainProcess(helper_args.wait_pid);
  }

  // CEF 151 的 Windows binary distribution 对 libcef.dll 使用 delay-load。
  // 按官方当前入口方式，在任何 CEF API 真正执行前显式加载并校验版本。
  cef_version_info_t version_info{};
  CEF_POPULATE_VERSION_INFO(&version_info);
  CefScopedLibraryLoader library_loader;
  {
    cef::logging::ScopedEarlySupport early_logging({});
    if (!library_loader.LoadInSubProcessAssert(&version_info)) {
      const std::filesystem::path libcef_path = util::ExeDir() / L"libcef.dll";
      if (!library_loader.LoadInMainAssert(
              libcef_path.c_str(), nullptr, true, &version_info)) {
        return 6;
      }
    }
  }

  const std::filesystem::path exe_dir = util::ExeDir();
  const std::filesystem::path ini_path = exe_dir / L"set.ini";
  auto config = std::make_shared<Config>(ini_path);
  UpgradeConfig(config);
  CefRefPtr<PersonalCefApp> app = new PersonalCefApp(config);

  CefMainArgs main_args(instance);

  // CEF 的 renderer/GPU/utility 子进程也会再次进入当前 EXE；必须先让
  // CefExecuteProcess 判断进程类型，再做单实例检查。
  const int subprocess_code = CefExecuteProcess(main_args, app, nullptr);
  if (subprocess_code >= 0) {
    return subprocess_code;
  }

  HANDLE mutex = ::CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
  if (!mutex) {
    return 2;
  }
  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND existing = ::FindWindowW(kMainWindowClass, nullptr)) {
      ::PostMessageW(existing, WM_APP_RESTORE_WINDOW, 0, 0);
    }
    ::CloseHandle(mutex);
    return 0;
  }

  EnableBestDpiAwareness();
  SetChatGptTaskbarIdentity();
  ::OleInitialize(nullptr);

  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = static_cast<DWORD>(sizeof(controls));
  controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
  ::InitCommonControlsEx(&controls);

  const std::filesystem::path runtime_dir = exe_dir / L"runtime";
  const std::filesystem::path cache_dir = runtime_dir / L"cef-cache";
  util::EnsureDir(cache_dir);

  CefSettings settings;
  settings.no_sandbox = true;
  settings.multi_threaded_message_loop = true;
  settings.persist_session_cookies = true;
  settings.log_severity = LOGSEVERITY_WARNING;
  // 登录 helper 必须与主程序使用完全相同的 CEF profile。
  // 不再把 runtime 作为 root user-data-dir，避免 Cookie/Local State 分裂到两层目录。
  CefString(&settings.root_cache_path) = cache_dir.wstring();
  CefString(&settings.cache_path) = cache_dir.wstring();
  CefString(&settings.log_file) = (runtime_dir / L"cef.log").wstring();

  const int debug_port =
      config->GetInt(L"browser", L"remote_debugging_port", 0);
  if (debug_port >= 1024 && debug_port <= 65535) {
    settings.remote_debugging_port = debug_port;
  }

  if (!CefInitialize(main_args, settings, app, nullptr)) {
    ::MessageBoxW(nullptr,
                  L"CEF 初始化失败。请确认 CEF 运行文件已由 CMake 复制到 EXE 目录。",
                  L"Personal CEF · ChatGPT", MB_OK | MB_ICONERROR);
    ::OleUninitialize();
    ::CloseHandle(mutex);
    return 3;
  }

  int exit_code = 0;
  {
    MainWindow window;
    if (!window.Create(instance, config)) {
      exit_code = 4;
    } else {
      MSG msg{};
      while (true) {
        const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
          if (result == -1) exit_code = 5;
          break;
        }

        // OnBeforeClose 可能发生在顶层 HWND 已销毁之后；此时
        // ClientHandler 会退回 PostThreadMessage，确保主线程仍能结束。
        if (!msg.hwnd && msg.message == WM_APP_BROWSER_CLOSED) {
          break;
        }
        if (window.PreTranslateMessage(&msg)) {
          continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
      }
    }
  }

  CefShutdown();
  ::OleUninitialize();
  ::CloseHandle(mutex);
  if (helper_args.return_main) {
    RestartMainApp();
  }
  return exit_code;
}
