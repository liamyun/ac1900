  } else if (hwnd_) {
    ::DestroyWindow(hwnd_);
    ::PostQuitMessage(0);
  }
}

bool MainWindow::PreTranslateMessage(MSG* msg) {
  if (!msg || msg->message != WM_KEYDOWN) return false;
  if ((::GetKeyState(VK_CONTROL) & 0x8000) && msg->wParam == 'L') {
    ::SetFocus(address_);
    ::SendMessageW(address_, EM_SETSEL, 0, -1);
    return true;
  }
  if (msg->wParam == VK_F5 && handler_) {
    handler_->Reload();
    return true;
  }
  if ((::GetKeyState(VK_MENU) & 0x8000) && msg->wParam == VK_LEFT && handler_) {
    handler_->GoBack();
    return true;
  }
  if ((::GetKeyState(VK_MENU) & 0x8000) && msg->wParam == VK_RIGHT && handler_) {
    handler_->GoForward();
    return true;
  }
  return false;
}

LRESULT CALLBACK MainWindow::AddressSubclassProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR ref_data) {
  auto* self = reinterpret_cast<MainWindow*>(ref_data);
  if (message == WM_KEYDOWN && wparam == VK_RETURN && self) {
    self->NavigateFromAddress();
    return 0;
  }
  return ::DefSubclassProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message,
                                     WPARAM wparam, LPARAM lparam) {
  MainWindow* self = reinterpret_cast<MainWindow*>(
      ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<MainWindow*>(cs->lpCreateParams);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    if (self) self->hwnd_ = hwnd;
  }
  if (self) {
    return self->HandleMessage(message, wparam, lparam);
  }
  return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      Layout();
      return 0;

    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      if (suggested) {
        ::SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
      }
      Layout();
      return 0;
    }

    case WM_SETTINGCHANGE:
      if (theme_mode_ == ThemeMode::kSystem) {
        ApplyTheme();
      }
      break;

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = ::BeginPaint(hwnd_, &ps);
      RECT rc{};
      ::GetClientRect(hwnd_, &rc);
      if (window_brush_) ::FillRect(dc, &rc, window_brush_);
      RECT top = rc;
      top.bottom = std::min<LONG>(rc.bottom, static_cast<LONG>(Scale(48)));
      if (toolbar_brush_) ::FillRect(dc, &top, toolbar_brush_);
      RECT bottom = rc;
      bottom.top = std::max<LONG>(rc.top, rc.bottom - static_cast<LONG>(Scale(24)));
      if (status_brush_) ::FillRect(dc, &bottom, status_brush_);
      ::EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      HWND control = reinterpret_cast<HWND>(lparam);
      if (control == status_) {
        ::SetTextColor(dc, muted_text_color_);
        ::SetBkMode(dc, OPAQUE);
        const bool dark = DarkThemeActive();
        ::SetBkColor(dc, dark ? RGB(27, 28, 31) : RGB(245, 246, 247));
        return reinterpret_cast<LRESULT>(status_brush_);
      }
      break;
    }

    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      if (reinterpret_cast<HWND>(lparam) == address_) {
        ::SetTextColor(dc, edit_text_color_);
        ::SetBkColor(dc, edit_bg_color_);
        return reinterpret_cast<LRESULT>(edit_brush_);
      }
      break;
    }

    case WM_SYSCOMMAND:
      if ((wparam & 0xFFF0) == SC_MINIMIZE && !exiting_) {
        HideToTray();
        return 0;
      }
      break;

    case WM_CLOSE:
      if (!exiting_ && !(handler_ && handler_->IsClosing())) {
        HideToTray();
        return 0;
      }
      // CEF 的 DoClose 已经允许真正销毁顶层窗口。
      break;

    case WM_COMMAND: {
      switch (LOWORD(wparam)) {
        case IDC_BACK:
          if (handler_) handler_->GoBack();
          return 0;
        case IDC_FORWARD:
          if (handler_) handler_->GoForward();
          return 0;
        case IDC_RELOAD:
          if (handler_) {
            if (browser_loading_) handler_->StopLoad();
            else handler_->Reload();
          }
          return 0;
        case IDC_HOME:
          if (handler_) handler_->Navigate(home_url_);
          return 0;
        case IDC_GO:
          NavigateFromAddress();
          return 0;
        case IDM_TRAY_SHOW:
          RestoreFromTray();
          return 0;
        case IDM_TRAY_HIDE:
          HideToTray();
          return 0;
        case IDM_TRAY_DATA:
          OpenFolder(data_root_);
          return 0;
        case IDM_TRAY_CAPTURE:
          OpenFolder(capture_dir_);
          return 0;
        case IDM_TRAY_CAPTURE_TOGGLE:
          ToggleCapture();
          return 0;
        case IDM_THEME_SYSTEM:
          SetTheme(ThemeMode::kSystem, true);
          return 0;
        case IDM_THEME_LIGHT:
          SetTheme(ThemeMode::kLight, true);
          return 0;
        case IDM_THEME_DARK:
          SetTheme(ThemeMode::kDark, true);
          return 0;
        case IDM_TRAY_EXIT:
          RequestExit();
          return 0;
      }
      break;
    }

    case kTrayMessage:
      if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) {
        RestoreFromTray();
      } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
        POINT pt{};
        ::GetCursorPos(&pt);
        ShowTrayMenu(pt);
      }
      return 0;

    case WM_APP_RESTORE_WINDOW:
      RestoreFromTray();
      return 0;

    case WM_APP_BROWSER_CREATED:
      browser_hwnd_ = reinterpret_cast<HWND>(wparam);
      Layout();
      return 0;

    case WM_APP_ADDRESS_CHANGED: {
      std::unique_ptr<std::wstring> text(
          reinterpret_cast<std::wstring*>(lparam));
      if (address_ && text) {
        ::SetWindowTextW(address_, text->c_str());
      }
      return 0;
    }

    case WM_APP_TITLE_CHANGED: {
      std::unique_ptr<std::wstring> text(
          reinterpret_cast<std::wstring*>(lparam));
      UpdateWindowTitle(text ? *text : L"");
      return 0;
    }

    case WM_APP_LOADING_CHANGED:
      UpdateNavButtons(wparam != 0, (lparam & 1) != 0, (lparam & 2) != 0);
      return 0;

    case WM_APP_BROWSER_CLOSED:
      handler_ = nullptr;
      RemoveTrayIcon();
      if (::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
      }
      ::PostQuitMessage(0);
      return 0;

    case WM_DESTROY:
      RemoveTrayIcon();
      // 正常 CEF 关闭时等 OnBeforeClose 再退出消息循环；否则直接退出。
      if (!handler_ || !(handler_->IsClosing())) {
        ::PostQuitMessage(0);
      }
      return 0;
  }

  return ::DefWindowProcW(hwnd_, message, wparam, lparam);
}
