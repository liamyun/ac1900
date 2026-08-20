#pragma once

#include "include/cef_browser.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace client::personal {

// r11：托盘只管理 CEF Chrome runtime 创建出的原生顶层 HWND。
// 不再依赖 cefclient ViewsWindow，也不参与标签页实现。
class TrayController {
 public:
  static TrayController& Get();

  bool AcquireSingleInstance();

  void AttachBrowser(CefRefPtr<CefBrowser> browser);
  void DetachBrowser(CefRefPtr<CefBrowser> browser);

  void RequestExit();
  bool exit_requested() const { return exit_requested_; }

  void ApplyConfiguredTheme(bool notify_views = true);

 private:
  TrayController() = default;
  ~TrayController();

  TrayController(const TrayController&) = delete;
  TrayController& operator=(const TrayController&) = delete;

  bool EnsureMessageWindow();
  void ApplyWindowIcon();
  void AddIcon();
  void RemoveIcon();
  void ShowWindow();
  void HideWindow();
  void ShowMenu(POINT pt);
  void OpenPath(const std::filesystem::path& path);
  void ApplyTheme(const std::string& theme, bool notify_views = true);
  void PublishMainWindowHandle(HWND hwnd);
  void ClearPublishedMainWindowHandle();
  bool CloseToTrayEnabled() const;
  bool MinimizeToTrayEnabled() const;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK BrowserSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                               LPARAM lparam, UINT_PTR subclass_id,
                                               DWORD_PTR ref_data);
  LRESULT OnBrowserWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  CefRefPtr<CefBrowser> primary_browser_;
  HWND managed_hwnd_ = nullptr;
  HWND message_hwnd_ = nullptr;
  HICON tray_icon_ = nullptr;
  bool exit_requested_ = false;
  bool icon_added_ = false;
  bool subclass_installed_ = false;

  HANDLE instance_mutex_ = nullptr;
  HANDLE instance_mapping_ = nullptr;
  void* instance_state_ = nullptr;
};

}  // namespace client::personal
