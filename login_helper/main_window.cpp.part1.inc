#include "main_window.h"

#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <string>

#include "client_handler.h"
#include "config.h"
#include "resource.h"
#include "ui_messages.h"
#include "util.h"

namespace {

constexpr wchar_t kWindowClass[] = L"PersonalCEF151.LoginHelperWindow";
constexpr wchar_t kWindowTitle[] = L"ChatGPT Google 登录 — r6 兼容窗口";
constexpr UINT kTrayMessage = WM_APP + 100;
constexpr UINT kTrayId = 1;

std::wstring ProxyStatus(const Config& config) {
  if (!config.GetBool(L"proxy", L"enabled", false)) {
    return L"直连";
  }
  const std::wstring host = config.GetWString(L"proxy", L"host", L"");
  const int port = config.GetInt(L"proxy", L"port", 0);
  const std::wstring type = config.GetWString(L"proxy", L"type", L"socks5");
  if (host.empty() || port <= 0 || port > 65535) {
    return L"代理配置无效";
  }
  std::wstring label = type.empty() ? L"SOCKS5" : type;
  std::transform(label.begin(), label.end(), label.begin(),
                 [](wchar_t c) {
                   return static_cast<wchar_t>(std::towupper(c));
                 });
  return label + L" " + host + L":" + std::to_wstring(port);
}

std::string NormalizeUrl(std::wstring value) {
  value = util::Trim(std::move(value));
  if (value.empty()) {
    return {};
  }
  std::string url = util::WideToUtf8(value);
  if (url.find("://") == std::string::npos &&
      url.rfind("data:", 0) != 0 && url.rfind("about:", 0) != 0 &&
      url.rfind("file:", 0) != 0) {
    url = "https://" + url;
  }
  return url;
}

bool SystemUsesDarkApps() {
  DWORD value = 1;
  DWORD bytes = sizeof(value);
  const LONG rc = ::RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &bytes);
  return rc == ERROR_SUCCESS && value == 0;
}

void SetImmersiveDarkTitleBar(HWND hwnd, bool dark) {
  if (!hwnd) return;
  HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return;
  using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
      ::GetProcAddress(dwm, "DwmSetWindowAttribute"));
  if (fn) {
    const BOOL enabled = dark ? TRUE : FALSE;
    // Windows 11/新 Windows 10 使用 20，部分旧版本使用 19。
    if (FAILED(fn(hwnd, 20, &enabled, sizeof(enabled)))) {
      fn(hwnd, 19, &enabled, sizeof(enabled));
    }
    // Windows 11 圆角窗口。
    const DWORD round_preference = 2;  // DWMWCP_ROUND
    fn(hwnd, 33, &round_preference, sizeof(round_preference));
  }
  ::FreeLibrary(dwm);
}

void ApplyExplorerTheme(HWND hwnd, bool dark) {
  if (!hwnd) return;
  HMODULE ux = ::LoadLibraryW(L"uxtheme.dll");
  if (!ux) return;
  using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
  auto fn = reinterpret_cast<SetWindowThemeFn>(
      ::GetProcAddress(ux, "SetWindowTheme"));
  if (fn) {
    fn(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  }
  ::FreeLibrary(ux);
}

}  // namespace

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
  RemoveTrayIcon();
  if (address_) {
    ::RemoveWindowSubclass(address_, AddressSubclassProc, 1);
  }
  if (ui_font_) {
    ::DeleteObject(ui_font_);
  }
  if (app_icon_large_) ::DestroyIcon(app_icon_large_);
  if (app_icon_small_) ::DestroyIcon(app_icon_small_);
  if (window_brush_) ::DeleteObject(window_brush_);
  if (toolbar_brush_) ::DeleteObject(toolbar_brush_);
  if (status_brush_) ::DeleteObject(status_brush_);
  if (edit_brush_) ::DeleteObject(edit_brush_);
}

bool MainWindow::RegisterWindowClass() {
  WNDCLASSEXW wc{};
  wc.cbSize = static_cast<UINT>(sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance_;
  wc.hIcon = static_cast<HICON>(::LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  wc.hIconSm = static_cast<HICON>(::LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;  // WM_PAINT 自己画，便于亮/暗皮肤即时切换。
  wc.lpszClassName = kWindowClass;

  if (::RegisterClassExW(&wc)) {
    return true;
  }
  return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void MainWindow::LoadAppIcons() {
  if (!hwnd_) return;
  app_icon_large_ = static_cast<HICON>(::LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON),
      LR_DEFAULTCOLOR));
  app_icon_small_ = static_cast<HICON>(::LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  if (app_icon_large_) {
    ::SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(app_icon_large_));
  }
  if (app_icon_small_) {
    ::SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(app_icon_small_));
  }
}

bool MainWindow::Create(HINSTANCE instance, std::shared_ptr<Config> config) {
  instance_ = instance;
  config_ = std::move(config);
  if (!RegisterWindowClass()) {
    return false;
  }

  home_url_ = config_->GetString(
      L"browser", L"home_url", "https://chatgpt.com/");
  capture_enabled_ = config_->GetBool(L"capture", L"enabled", false);
  proxy_status_ = ProxyStatus(*config_);
  theme_mode_ = ParseTheme(config_->GetWString(L"ui", L"theme", L"system"));
  data_root_ = config_->GetPath(L"market", L"data_root", util::ExeDir());
  capture_dir_ = config_->GetPath(
      L"capture", L"directory", util::ExeDir() / L"capture");

  const int width = std::max(900, config_->GetInt(L"window", L"width", 1280));
  const int height = std::max(640, config_->GetInt(L"window", L"height", 860));

  hwnd_ = ::CreateWindowExW(
      0, kWindowClass, kWindowTitle,
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, width, height,
      nullptr, nullptr, instance_, this);
  if (!hwnd_) {
    return false;
  }

  // 不只依赖窗口类图标：显式 WM_SETICON，确保任务栏使用内嵌图标，
  // 不再退回成 Windows 默认的白色窗口图标。
  LoadAppIcons();
  CreateControls();
  ApplyTheme();
  AddTrayIcon();

  handler_ = new ClientHandler(hwnd_, config_);
  RECT browser_bounds{};
  ::GetClientRect(hwnd_, &browser_bounds);
  browser_bounds.top = Scale(48);
  browser_bounds.bottom -= Scale(24);
  if (!handler_->CreateBrowser(hwnd_, browser_bounds, home_url_)) {
    ::MessageBoxW(hwnd_, L"CEF 浏览器创建失败。", L"Personal CEF",
                  MB_OK | MB_ICONERROR);
    return false;
  }

  const bool start_hidden =
      config_->GetBool(L"window", L"start_hidden", false);
  if (start_hidden) {
    ::ShowWindow(hwnd_, SW_HIDE);
  } else {
    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);
  }
  return true;
}

int MainWindow::Scale(int logical_px) const {
  const UINT dpi = hwnd_ ? ::GetDpiForWindow(hwnd_) : 96;
  return ::MulDiv(logical_px, static_cast<int>(dpi ? dpi : 96), 96);
}

void MainWindow::CreateControls() {
  const int font_height = -Scale(13);
  ui_font_ = ::CreateFontW(
      font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      L"Segoe UI Variable Text");

  auto make_button = [&](int id, const wchar_t* text) {
    HWND h = ::CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_, nullptr);
    ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    return h;
  };

  back_ = make_button(IDC_BACK, L"←");
  forward_ = make_button(IDC_FORWARD, L"→");
  reload_ = make_button(IDC_RELOAD, L"↻");
  home_ = make_button(IDC_HOME, L"⌂");
  go_ = make_button(IDC_GO, L"打开");

  address_ = ::CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
      0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(IDC_ADDRESS), instance_, nullptr);
  ::SendMessageW(address_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  ::SetWindowSubclass(address_, AddressSubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

  status_ = ::CreateWindowExW(
      0, L"STATIC", L"就绪",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
