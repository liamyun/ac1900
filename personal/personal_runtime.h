#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_response_filter.h"
#include "tests/cefclient/browser/root_window.h"

class Config;
class MailService;
class MarketDataEngine;
class NetworkCapture;
class Scheduler;

namespace client::personal {

// 个人功能只挂在官方 cefclient 的扩展点上。
// 浏览器/窗口/线程/消息循环全部继续由官方 cefclient 管理。
class Runtime {
 public:
  static Runtime& Get();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // 仅 browser process 调用。读取 set.ini，并把 URL/代理/cache/UI 参数
  // 追加到官方 CefCommandLine；不替换 CEF 自己的消息循环。
  void ConfigureCommandLine(CefRefPtr<CefCommandLine> command_line);

  // 官方主消息循环退出后、CefShutdown 前：只停止会创建 Browser/RootWindow
  // 的后台 Scheduler；网络回调相关对象继续活到 CefShutdown 完成。
  void PrepareForCefShutdown();

  // CefShutdown 完成后再释放采集器/行情/配置等对象。
  void Shutdown();

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser);
  void OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect);

  // CefResourceRequestHandler 扩展点。
  void OnBeforeResourceLoad(CefRefPtr<CefRequest> request);
  CefRefPtr<CefResponseFilter> GetResponseFilter(
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response);
  void OnResourceLoadComplete(CefRefPtr<CefRequest> request,
                              CefRefPtr<CefResponse> response,
                              cef_urlrequest_status_t status,
                              int64_t received_content_length);

  // 定时行情：用官方 RootWindowManager 创建隐藏 Browser，而不是导航主 ChatGPT。
  scoped_refptr<RootWindow> OpenHiddenMarketWindow(const std::string& url);
  void CloseHiddenWindow(scoped_refptr<RootWindow> window);

  bool capture_enabled() const;
  void SetCaptureEnabled(bool enabled);

  std::string theme() const;
  void SetTheme(const std::string& theme);

  std::filesystem::path config_path() const;
  std::filesystem::path data_root() const;
  std::filesystem::path capture_dir() const;

 public:
  // 仅供 CefResponseFilter 持有；状态仍只由 Runtime 管理。
  struct ResponseState {
    std::string body;
    size_t limit = 0;
    bool truncated = false;
  };

 private:
  Runtime();
  ~Runtime();

  void EnsureConfig();
  void EnsureInitialized();
  bool IsChatGptFamilyUrl(const std::string& raw_url) const;
  void StartSchedulerIfNeeded();
  void StopScheduler();

  mutable std::mutex init_mutex_;
  bool initialized_ = false;
  bool shutdown_ = false;

  std::shared_ptr<Config> config_;
  std::unique_ptr<NetworkCapture> network_capture_;
  std::unique_ptr<MarketDataEngine> market_data_;
  std::unique_ptr<MailService> mail_service_;
  std::unique_ptr<Scheduler> scheduler_;

  mutable std::mutex browser_mutex_;
  int main_browser_id_ = 0;

  // 资源回调都在 CEF IO thread，但仍加锁，避免以后 API 调度变化时踩坑。
  std::mutex response_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<ResponseState>> responses_;
};

}  // namespace client::personal
