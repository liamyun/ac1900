#pragma once

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <string>

#include "include/cef_base.h"

class ClientHandler;
class Config;

class MainWindow {
 public:
  MainWindow();
  ~MainWindow();

  bool Create(HINSTANCE instance, std::shared_ptr<Config> config);
  bool PreTranslateMessage(MSG* msg);

  HWND hwnd() const { return hwnd_; }
  void RestoreFromTray();
  void RequestExit();

 private:
  enum class ThemeMode { kSystem, kLight, kDark };

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK AddressSubclassProc(HWND hwnd, UINT message,
                                              WPARAM wparam, LPARAM lparam,
                                              UINT_PTR subclass_id,
                                              DWORD_PTR ref_data);

  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool RegisterWindowClass();
  void LoadAppIcons();
  void CreateControls();
  void Layout();
  void NavigateFromAddress();
  void UpdateNavButtons(bool loading, bool can_back, bool can_forward);
  void UpdateCaptureUi();
  void ToggleCapture();
  void UpdateWindowTitle(const std::wstring& page_title);
  void HideToTray();
  void AddTrayIcon();
  void RemoveTrayIcon();
  void ShowTrayMenu(POINT point);
  void OpenFolder(const std::filesystem::path& path);

  void SetTheme(ThemeMode mode, bool save);
  void ApplyTheme();
  bool DarkThemeActive() const;
  static ThemeMode ParseTheme(const std::wstring& value);
  static const wchar_t* ThemeConfigValue(ThemeMode mode);

  int Scale(int logical_px) const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND back_ = nullptr;
  HWND forward_ = nullptr;
  HWND reload_ = nullptr;
  HWND home_ = nullptr;
  HWND address_ = nullptr;
  HWND go_ = nullptr;
  HWND status_ = nullptr;
  HWND browser_hwnd_ = nullptr;
  HFONT ui_font_ = nullptr;
  HICON app_icon_large_ = nullptr;
  HICON app_icon_small_ = nullptr;

  HBRUSH window_brush_ = nullptr;
  HBRUSH toolbar_brush_ = nullptr;
  HBRUSH status_brush_ = nullptr;
  HBRUSH edit_brush_ = nullptr;
  COLORREF text_color_ = RGB(32, 33, 36);
  COLORREF muted_text_color_ = RGB(80, 84, 88);
  COLORREF edit_text_color_ = RGB(32, 33, 36);
  COLORREF edit_bg_color_ = RGB(255, 255, 255);

  std::shared_ptr<Config> config_;
  CefRefPtr<ClientHandler> handler_;
  std::string home_url_;
  std::filesystem::path data_root_;
  std::filesystem::path capture_dir_;
  std::wstring proxy_status_;
  bool capture_enabled_ = false;
  ThemeMode theme_mode_ = ThemeMode::kSystem;

  NOTIFYICONDATAW tray_{};
  bool tray_added_ = false;
  bool exiting_ = false;
  bool browser_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
};
