#include "personal_tray_win.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "include/wrapper/cef_helpers.h"
#include "tests/cefclient/browser/resource.h"

#include "config.h"
#include "personal_runtime.h"
#include "util.h"

namespace client::personal {
namespace {

constexpr UINT kTrayMessage = WM_APP + 0x431;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kBrowserSubclassId = 0x50434546;

constexpr wchar_t kInstanceMutexName[] =
    L"Local\\PersonalCEF151.SingleInstance.r11";
constexpr wchar_t kInstanceMapName[] =
    L"Local\\PersonalCEF151.MainWindowState.r11";
constexpr DWORD kInstanceMagic = 0x50434546;  // 'PCEF'

struct SharedInstanceState {
  DWORD magic = 0;
  DWORD pid = 0;
  UINT_PTR main_hwnd = 0;
};

bool ActivateExistingMainWindow() {
  for (int attempt = 0; attempt < 80; ++attempt) {
    HANDLE mapping = ::OpenFileMappingW(FILE_MAP_READ, FALSE, kInstanceMapName);
    if (mapping) {
      auto* state = static_cast<const SharedInstanceState*>(
          ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                          sizeof(SharedInstanceState)));
      if (state) {
        const DWORD pid = state->pid;
        const HWND hwnd = reinterpret_cast<HWND>(state->main_hwnd);
        const bool valid = state->magic == kInstanceMagic && pid != 0 && hwnd &&
                           ::IsWindow(hwnd);
        if (valid) {
          DWORD hwnd_pid = 0;
          ::GetWindowThreadProcessId(hwnd, &hwnd_pid);
          if (hwnd_pid == pid) {
            if (::IsIconic(hwnd)) {
              ::ShowWindowAsync(hwnd, SW_RESTORE);
            }
            ::ShowWindowAsync(hwnd, SW_SHOW);
            ::BringWindowToTop(hwnd);
            ::SetForegroundWindow(hwnd);
            ::UnmapViewOfFile(state);
            ::CloseHandle(mapping);
            return true;
          }
        }
        ::UnmapViewOfFile(state);
      }
      ::CloseHandle(mapping);
    }
    ::Sleep(50);
  }
  return false;
}

constexpr UINT kCmdShow = 41001;
constexpr UINT kCmdHide = 41002;
constexpr UINT kCmdCapture = 41003;
constexpr UINT kCmdThemeSystem = 41010;
constexpr UINT kCmdThemeLight = 41011;
constexpr UINT kCmdThemeDark = 41012;
constexpr UINT kCmdOpenData = 41020;
constexpr UINT kCmdOpenCapture = 41021;
constexpr UINT kCmdOpenConfig = 41022;
constexpr UINT kCmdGoogleLogin = 41030;
constexpr UINT kCmdExit = 41099;

bool WindowsUsesLightApps() {
  HKEY key = nullptr;
  DWORD value = 1;
  DWORD size = sizeof(value);
  if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    ::RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                       reinterpret_cast<BYTE*>(&value), &size);
    ::RegCloseKey(key);
  }
  return value != 0;
}

void SetMenuCheck(HMENU menu, UINT id, bool checked) {
  ::CheckMenuItem(menu, id,
                  MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

}  // namespace

TrayController& TrayController::Get() {
  static TrayController instance;
  return instance;
}

TrayController::~TrayController() {
  if (managed_hwnd_ && subclass_installed_ && ::IsWindow(managed_hwnd_)) {
    ::RemoveWindowSubclass(managed_hwnd_, &TrayController::BrowserSubclassProc,
                           kBrowserSubclassId);
  }
  RemoveIcon();
  if (instance_state_) {
    ::UnmapViewOfFile(instance_state_);
  }
  if (instance_mapping_) {
    ::CloseHandle(instance_mapping_);
  }
  if (instance_mutex_) {
    ::CloseHandle(instance_mutex_);
  }
}

bool TrayController::AcquireSingleInstance() {
  if (instance_mutex_) {
    return true;
  }

  HANDLE mutex = ::CreateMutexW(nullptr, FALSE, kInstanceMutexName);
  if (!mutex) {
    return true;
  }
  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    ::CloseHandle(mutex);
    ActivateExistingMainWindow();
    return false;
  }

  instance_mutex_ = mutex;
  instance_mapping_ = ::CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
      static_cast<DWORD>(sizeof(SharedInstanceState)), kInstanceMapName);
  if (instance_mapping_) {
    instance_state_ = ::MapViewOfFile(instance_mapping_, FILE_MAP_ALL_ACCESS,
                                      0, 0, sizeof(SharedInstanceState));
    if (instance_state_) {
      auto* state = static_cast<SharedInstanceState*>(instance_state_);
      state->magic = kInstanceMagic;
      state->pid = ::GetCurrentProcessId();
      state->main_hwnd = 0;
    }
  }
  return true;
}

void TrayController::PublishMainWindowHandle(HWND hwnd) {
  if (!instance_state_) {
    return;
  }
  auto* state = static_cast<SharedInstanceState*>(instance_state_);
  state->main_hwnd = reinterpret_cast<UINT_PTR>(hwnd);
}

void TrayController::ClearPublishedMainWindowHandle() {
  if (!instance_state_) {
    return;
  }
  static_cast<SharedInstanceState*>(instance_state_)->main_hwnd = 0;
}

bool TrayController::EnsureMessageWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (message_hwnd_) {
    return true;
  }

  const wchar_t* class_name = L"PersonalCEF151_R11_TrayWindow";
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = &TrayController::WndProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  ::RegisterClassExW(&wc);

  message_hwnd_ = ::CreateWindowExW(
      0, class_name, L"PersonalCEF Tray", 0, 0, 0, 0, 0, HWND_MESSAGE,
      nullptr, wc.hInstance, this);
  return message_hwnd_ != nullptr;
}

void TrayController::AttachBrowser(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || managed_hwnd_) {
    return;  // 同一原生 Chrome 窗口的后续 Tab 不重复接管。
  }

  HWND hwnd = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  if (!hwnd) {
    return;
  }
  if (HWND root = ::GetAncestor(hwnd, GA_ROOT)) {
    hwnd = root;
  }

  primary_browser_ = browser;
  managed_hwnd_ = hwnd;
  exit_requested_ = false;

  EnsureMessageWindow();
  PublishMainWindowHandle(hwnd);

  subclass_installed_ =
      ::SetWindowSubclass(hwnd, &TrayController::BrowserSubclassProc,
                          kBrowserSubclassId,
                          reinterpret_cast<DWORD_PTR>(this)) == TRUE;

  ApplyWindowIcon();
  AddIcon();
  ApplyConfiguredTheme();
}

void TrayController::DetachBrowser(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!managed_hwnd_) {
    return;
  }

  if (subclass_installed_ && ::IsWindow(managed_hwnd_)) {
    ::RemoveWindowSubclass(managed_hwnd_, &TrayController::BrowserSubclassProc,
                           kBrowserSubclassId);
  }
  subclass_installed_ = false;
  RemoveIcon();
  ClearPublishedMainWindowHandle();
  primary_browser_ = nullptr;
  managed_hwnd_ = nullptr;
}

bool TrayController::CloseToTrayEnabled() const {
  if (Runtime::Get().config_path().empty()) {
    return false;
  }
  Config config(Runtime::Get().config_path());
  return config.GetBool(L"tray", L"close_to_tray", true);
}

bool TrayController::MinimizeToTrayEnabled() const {
  if (Runtime::Get().config_path().empty()) {
    return false;
  }
  Config config(Runtime::Get().config_path());
  return config.GetBool(L"tray", L"minimize_to_tray", true);
}

void TrayController::ApplyWindowIcon() {
  if (!managed_hwnd_) {
    return;
  }

  HINSTANCE module = ::GetModuleHandleW(nullptr);
  HICON big_icon = static_cast<HICON>(::LoadImageW(
      module, MAKEINTRESOURCEW(IDR_MAINFRAME), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  HICON small_icon = static_cast<HICON>(::LoadImageW(
      module, MAKEINTRESOURCEW(IDR_MAINFRAME), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR | LR_SHARED));
