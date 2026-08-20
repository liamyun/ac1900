      0, 0, 1, 1, hwnd_, reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);
  ::SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);

  UpdateNavButtons(false, false, false);
  Layout();
}

void MainWindow::Layout() {
  if (!hwnd_) return;
  RECT rc{};
  ::GetClientRect(hwnd_, &rc);
  const int w = std::max(1L, rc.right - rc.left);
  const int h = std::max(1L, rc.bottom - rc.top);

  const int pad = Scale(8);
  const int bar_h = Scale(48);
  const int status_h = Scale(24);
  const int btn = Scale(32);
  const int gap = Scale(6);
  const int go_w = Scale(56);

  int x = pad;
  const int y = (bar_h - btn) / 2;
  for (HWND button : {back_, forward_, reload_, home_}) {
    if (button) {
      ::MoveWindow(button, x, y, btn, btn, TRUE);
      x += btn + gap;
    }
  }

  const int go_x = std::max(x + Scale(120), w - pad - go_w);
  const int address_right = std::max(x + Scale(80), go_x - gap);
  if (address_) {
    ::MoveWindow(address_, x, y, std::max(1, address_right - x), btn, TRUE);
  }
  if (go_) {
    ::MoveWindow(go_, go_x, y, go_w, btn, TRUE);
  }
  if (status_) {
    ::MoveWindow(status_, pad, std::max(0, h - status_h),
                 std::max(1, w - pad * 2), status_h, TRUE);
  }
  if (browser_hwnd_) {
    const int browser_h = std::max(1, h - bar_h - status_h);
    ::MoveWindow(browser_hwnd_, 0, bar_h, w, browser_h, TRUE);
  }
}

void MainWindow::NavigateFromAddress() {
  if (!address_ || !handler_) return;
  const int len = ::GetWindowTextLengthW(address_);
  std::wstring value(static_cast<size_t>(len) + 1, L'\0');
  if (len > 0) {
    ::GetWindowTextW(address_, value.data(), len + 1);
  }
  value.resize(static_cast<size_t>(len));
  const std::string url = NormalizeUrl(std::move(value));
  if (!url.empty()) {
    handler_->Navigate(url);
  }
}

void MainWindow::UpdateNavButtons(bool loading,
                                  bool can_back,
                                  bool can_forward) {
  browser_loading_ = loading;
  can_go_back_ = can_back;
  can_go_forward_ = can_forward;
  if (back_) ::EnableWindow(back_, can_back);
  if (forward_) ::EnableWindow(forward_, can_forward);
  if (reload_) ::SetWindowTextW(reload_, loading ? L"×" : L"↻");
  UpdateCaptureUi();
}

void MainWindow::UpdateCaptureUi() {
  if (handler_) {
    capture_enabled_ = handler_->CaptureEnabled();
  }
  if (status_) {
    std::wstring text = browser_loading_ ? L"加载中…" : L"就绪";
    text += L"   ·   ";
    text += proxy_status_;
    text += L"   ·   明文采集：";
    text += capture_enabled_ ? L"开" : L"关";
    ::SetWindowTextW(status_, text.c_str());
  }
}

void MainWindow::ToggleCapture() {
  capture_enabled_ = !capture_enabled_;
  if (handler_) {
    handler_->SetCaptureEnabled(capture_enabled_);
  }
  if (config_) {
    config_->SetBool(L"capture", L"enabled", capture_enabled_);
  }
  UpdateCaptureUi();
}

void MainWindow::UpdateWindowTitle(const std::wstring& page_title) {
  std::wstring title = page_title.empty() ? kWindowTitle : page_title;
  if (title != kWindowTitle) {
    title += L" — Personal CEF";
  }
  ::SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::AddTrayIcon() {
  if (tray_added_ || !hwnd_) return;
  tray_ = {};
  tray_.cbSize = static_cast<DWORD>(sizeof(tray_));
  tray_.hWnd = hwnd_;
  tray_.uID = kTrayId;
  tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  tray_.uCallbackMessage = kTrayMessage;
  tray_.hIcon = static_cast<HICON>(::LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  ::StringCchCopyW(tray_.szTip, ARRAYSIZE(tray_.szTip), L"Google 登录 · r6 兼容窗口");
  tray_added_ = ::Shell_NotifyIconW(NIM_ADD, &tray_) == TRUE;
  if (tray_added_) {
    tray_.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &tray_);
  }
}

void MainWindow::RemoveTrayIcon() {
  if (tray_added_) {
    ::Shell_NotifyIconW(NIM_DELETE, &tray_);
    tray_added_ = false;
  }
  if (tray_.hIcon) {
    ::DestroyIcon(tray_.hIcon);
    tray_.hIcon = nullptr;
  }
}

void MainWindow::HideToTray() {
  if (!hwnd_) return;
  AddTrayIcon();
  ::ShowWindow(hwnd_, SW_HIDE);
}

void MainWindow::RestoreFromTray() {
  if (!hwnd_) return;
  ::ShowWindow(hwnd_, SW_RESTORE);
  ::ShowWindow(hwnd_, SW_SHOW);
  ::SetForegroundWindow(hwnd_);
}

void MainWindow::OpenFolder(const std::filesystem::path& path) {
  util::EnsureDir(path);
  ::ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

MainWindow::ThemeMode MainWindow::ParseTheme(const std::wstring& value) {
  std::wstring v = value;
  std::transform(v.begin(), v.end(), v.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  if (v == L"light") return ThemeMode::kLight;
  if (v == L"dark") return ThemeMode::kDark;
  return ThemeMode::kSystem;
}

const wchar_t* MainWindow::ThemeConfigValue(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kLight: return L"light";
    case ThemeMode::kDark: return L"dark";
    default: return L"system";
  }
}

bool MainWindow::DarkThemeActive() const {
  if (theme_mode_ == ThemeMode::kDark) return true;
  if (theme_mode_ == ThemeMode::kLight) return false;
  return SystemUsesDarkApps();
}

void MainWindow::SetTheme(ThemeMode mode, bool save) {
  theme_mode_ = mode;
  if (save && config_) {
    config_->SetWString(L"ui", L"theme", ThemeConfigValue(mode));
  }
  ApplyTheme();
}

void MainWindow::ApplyTheme() {
  if (!hwnd_) return;
  const bool dark = DarkThemeActive();

  if (window_brush_) ::DeleteObject(window_brush_);
  if (toolbar_brush_) ::DeleteObject(toolbar_brush_);
  if (status_brush_) ::DeleteObject(status_brush_);
  if (edit_brush_) ::DeleteObject(edit_brush_);

  const COLORREF window_bg = dark ? RGB(24, 24, 27) : RGB(255, 255, 255);
  const COLORREF toolbar_bg = dark ? RGB(32, 33, 36) : RGB(248, 249, 250);
  const COLORREF status_bg = dark ? RGB(27, 28, 31) : RGB(245, 246, 247);
  edit_bg_color_ = dark ? RGB(46, 47, 51) : RGB(255, 255, 255);
  text_color_ = dark ? RGB(232, 234, 237) : RGB(32, 33, 36);
  muted_text_color_ = dark ? RGB(189, 193, 198) : RGB(95, 99, 104);
  edit_text_color_ = dark ? RGB(242, 243, 245) : RGB(32, 33, 36);

  window_brush_ = ::CreateSolidBrush(window_bg);
  toolbar_brush_ = ::CreateSolidBrush(toolbar_bg);
  status_brush_ = ::CreateSolidBrush(status_bg);
  edit_brush_ = ::CreateSolidBrush(edit_bg_color_);

  SetImmersiveDarkTitleBar(hwnd_, dark);
  for (HWND control : {back_, forward_, reload_, home_, go_, address_}) {
    ApplyExplorerTheme(control, dark);
  }

  ::InvalidateRect(hwnd_, nullptr, TRUE);
  if (address_) ::InvalidateRect(address_, nullptr, TRUE);
  if (status_) ::InvalidateRect(status_, nullptr, TRUE);
}

void MainWindow::ShowTrayMenu(POINT point) {
  HMENU menu = ::CreatePopupMenu();
  if (!menu) return;

  ::AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"显示窗口");
  ::AppendMenuW(menu, MF_STRING, IDM_TRAY_HIDE, L"隐藏窗口");
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  HMENU theme = ::CreatePopupMenu();
  if (theme) {
    ::AppendMenuW(theme,
                  MF_STRING | (theme_mode_ == ThemeMode::kSystem ? MF_CHECKED : 0),
                  IDM_THEME_SYSTEM, L"跟随系统");
    ::AppendMenuW(theme,
                  MF_STRING | (theme_mode_ == ThemeMode::kLight ? MF_CHECKED : 0),
                  IDM_THEME_LIGHT, L"浅色");
    ::AppendMenuW(theme,
                  MF_STRING | (theme_mode_ == ThemeMode::kDark ? MF_CHECKED : 0),
                  IDM_THEME_DARK, L"深色");
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), L"界面主题");
  }

  ::AppendMenuW(menu, MF_STRING | (capture_enabled_ ? MF_CHECKED : 0),
                IDM_TRAY_CAPTURE_TOGGLE, L"明文采集");
  ::AppendMenuW(menu, MF_STRING, IDM_TRAY_CAPTURE, L"打开抓包目录");
  ::AppendMenuW(menu, MF_STRING, IDM_TRAY_DATA, L"打开数据目录");
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"退出");

  ::SetForegroundWindow(hwnd_);
  ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   point.x, point.y, 0, hwnd_, nullptr);
  ::DestroyMenu(menu);
}

void MainWindow::RequestExit() {
  if (exiting_) return;
  exiting_ = true;
  RemoveTrayIcon();
  if (handler_) {
    handler_->CloseBrowser(false);
