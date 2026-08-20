
  if (big_icon) {
    ::SendMessageW(managed_hwnd_, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(big_icon));
  }
  if (small_icon) {
    ::SendMessageW(managed_hwnd_, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(small_icon));
  }
  tray_icon_ = small_icon ? small_icon : big_icon;
}

void TrayController::RequestExit() {
  exit_requested_ = true;
  if (managed_hwnd_ && ::IsWindow(managed_hwnd_)) {
    ::PostMessageW(managed_hwnd_, WM_CLOSE, 0, 0);
  } else if (primary_browser_) {
    primary_browser_->GetHost()->CloseBrowser(false);
  }
}

void TrayController::AddIcon() {
  if (!message_hwnd_ || icon_added_) {
    return;
  }
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = message_hwnd_;
  nid.uID = kTrayId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = kTrayMessage;
  nid.hIcon = tray_icon_ ? tray_icon_ : ::LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, L"ChatGPT - Personal CEF");
  icon_added_ = ::Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
}

void TrayController::RemoveIcon() {
  if (!message_hwnd_ || !icon_added_) {
    return;
  }
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = message_hwnd_;
  nid.uID = kTrayId;
  ::Shell_NotifyIconW(NIM_DELETE, &nid);
  icon_added_ = false;
}

void TrayController::ShowWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!managed_hwnd_ || !::IsWindow(managed_hwnd_)) {
    return;
  }
  if (::IsIconic(managed_hwnd_)) {
    ::ShowWindow(managed_hwnd_, SW_RESTORE);
  } else {
    ::ShowWindow(managed_hwnd_, SW_SHOW);
  }
  ApplyWindowIcon();
  ::BringWindowToTop(managed_hwnd_);
  ::SetForegroundWindow(managed_hwnd_);
}

void TrayController::HideWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (managed_hwnd_ && ::IsWindow(managed_hwnd_)) {
    ::ShowWindow(managed_hwnd_, SW_HIDE);
  }
}

void TrayController::OpenPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  if (std::filesystem::is_directory(path)) {
    util::EnsureDir(path);
  }
  ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void LaunchR6GoogleLoginAndExit() {
  const auto helper = util::ExeDir() / L"PersonalCEFLogin.exe";
  if (!std::filesystem::exists(helper)) {
    ::MessageBoxW(nullptr, L"未找到 PersonalCEFLogin.exe。请重新生成 Release/Debug。",
                  L"PersonalCEF", MB_OK | MB_ICONERROR);
    return;
  }
  const std::wstring args =
      L"--wait-pid=" + std::to_wstring(::GetCurrentProcessId()) +
      L" --return-main";
  HINSTANCE result = ::ShellExecuteW(nullptr, L"open", helper.c_str(), args.c_str(),
                                     util::ExeDir().c_str(), SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    ::MessageBoxW(nullptr, L"启动 r6 登录窗口失败。", L"PersonalCEF",
                  MB_OK | MB_ICONERROR);
    return;
  }
  TrayController::Get().RequestExit();
}

void TrayController::ShowMenu(POINT pt) {
  CEF_REQUIRE_UI_THREAD();
  HMENU menu = ::CreatePopupMenu();
  if (!menu) {
    return;
  }

  ::AppendMenuW(menu, MF_STRING, kCmdShow, L"显示 ChatGPT");
  ::AppendMenuW(menu, MF_STRING, kCmdHide, L"隐藏到托盘");
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(menu,
                MF_STRING | (Runtime::Get().capture_enabled() ? MF_CHECKED : 0),
                kCmdCapture, L"明文采集");

  HMENU theme_menu = ::CreatePopupMenu();
  ::AppendMenuW(theme_menu, MF_STRING, kCmdThemeSystem, L"跟随系统");
  ::AppendMenuW(theme_menu, MF_STRING, kCmdThemeLight, L"浅色");
  ::AppendMenuW(theme_menu, MF_STRING, kCmdThemeDark, L"深色");
  const std::string current_theme = Runtime::Get().theme();
  SetMenuCheck(theme_menu, kCmdThemeSystem, current_theme == "system");
  SetMenuCheck(theme_menu, kCmdThemeLight, current_theme == "light");
  SetMenuCheck(theme_menu, kCmdThemeDark, current_theme == "dark");
  ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme_menu), L"界面主题");

  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(menu, MF_STRING, kCmdGoogleLogin, L"Google 登录（r6兼容窗口）");
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(menu, MF_STRING, kCmdOpenData, L"打开数据目录");
  ::AppendMenuW(menu, MF_STRING, kCmdOpenCapture, L"打开采集目录");
  ::AppendMenuW(menu, MF_STRING, kCmdOpenConfig, L"打开 set.ini");
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");

  ::SetForegroundWindow(message_hwnd_);
  const UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, message_hwnd_, nullptr);
  ::DestroyMenu(menu);

  switch (cmd) {
    case kCmdShow:
      ShowWindow();
      break;
    case kCmdHide:
      HideWindow();
      break;
    case kCmdCapture:
      Runtime::Get().SetCaptureEnabled(!Runtime::Get().capture_enabled());
      break;
    case kCmdThemeSystem:
      Runtime::Get().SetTheme("system");
      ApplyTheme("system");
      break;
    case kCmdThemeLight:
      Runtime::Get().SetTheme("light");
      ApplyTheme("light");
      break;
    case kCmdThemeDark:
      Runtime::Get().SetTheme("dark");
      ApplyTheme("dark");
      break;
    case kCmdGoogleLogin:
      LaunchR6GoogleLoginAndExit();
      break;
    case kCmdOpenData:
      OpenPath(Runtime::Get().data_root());
      break;
    case kCmdOpenCapture:
      OpenPath(Runtime::Get().capture_dir());
      break;
    case kCmdOpenConfig:
      OpenPath(Runtime::Get().config_path());
      break;
    case kCmdExit:
      RequestExit();
      break;
    default:
      break;
  }
}

void TrayController::ApplyConfiguredTheme(bool notify_views) {
  ApplyTheme(Runtime::Get().theme(), notify_views);
}

void TrayController::ApplyTheme(const std::string& requested, bool notify_views) {
  CEF_REQUIRE_UI_THREAD();
  if (!managed_hwnd_) {
    return;
  }

  std::string theme = requested;
  if (theme == "system") {
    theme = WindowsUsesLightApps() ? "light" : "dark";
  }
  const BOOL use_dark = theme == "dark" ? TRUE : FALSE;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
  ::DwmSetWindowAttribute(managed_hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &use_dark, sizeof(use_dark));
  (void)notify_views;  // r11 没有自建 ViewsWindow，Chrome UI 自己管理主题。
}

LRESULT CALLBACK TrayController::BrowserSubclassProc(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data) {
  auto* self = reinterpret_cast<TrayController*>(ref_data);
  if (!self) {
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
  }
  return self->OnBrowserWindowMessage(hwnd, msg, wparam, lparam);
}

LRESULT TrayController::OnBrowserWindowMessage(HWND hwnd,
                                               UINT msg,
                                               WPARAM wparam,
                                               LPARAM lparam) {
  if (msg == WM_CLOSE && !exit_requested_ && CloseToTrayEnabled()) {
    ::ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  if (msg == WM_SIZE) {
    ApplyWindowIcon();
    if (wparam == SIZE_MINIMIZED && !exit_requested_ &&
        MinimizeToTrayEnabled()) {
      ::ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
  }
  if (msg == WM_NCDESTROY) {
    subclass_installed_ = false;
  }
  return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK TrayController::WndProc(HWND hwnd,
                                         UINT msg,
                                         WPARAM wparam,
                                         LPARAM lparam) {
  TrayController* self = reinterpret_cast<TrayController*>(
      ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<TrayController*>(cs->lpCreateParams);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  return self ? self->OnMessage(hwnd, msg, wparam, lparam)
              : ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT TrayController::OnMessage(HWND hwnd,
                                  UINT msg,
                                  WPARAM wparam,
                                  LPARAM lparam) {
  if (msg == kTrayMessage && wparam == kTrayId) {
    const UINT event = LOWORD(lparam);
    if (event == WM_LBUTTONDBLCLK || event == WM_LBUTTONUP) {
      ShowWindow();
      return 0;
    }
    if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
      POINT pt{};
      ::GetCursorPos(&pt);
      ShowMenu(pt);
      return 0;
    }
  }
  return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace client::personal
