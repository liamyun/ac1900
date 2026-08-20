#pragma once

#include <windows.h>

// windowsx.h/WinUser.h define helper macros with the same names as CEF DOM
// methods. Undefine them before parsing CEF headers.
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetNextSibling
#undef GetNextSibling
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"

#include "mail_service.h"
#include "market_data.h"
#include "network_capture.h"

class Config;
class Scheduler;

class ClientHandler : public CefClient,
                      public CefDisplayHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefRequestHandler,
                      public CefResourceRequestHandler {
 public:
  ClientHandler(HWND main_hwnd, std::shared_ptr<Config> config);
  ~ClientHandler() override;

  bool CreateBrowser(HWND parent, const RECT& bounds, const std::string& url);

  void Navigate(const std::string& url);
  void GoBack();
  void GoForward();
  void Reload();
  void StopLoad();
  void CloseBrowser(bool force_close);
  bool IsClosing() const { return is_closing_.load(); }

  // 明文采集可在 Win32 主线程即时开关；底层使用 atomic，不需要重启 CEF。
  bool CaptureEnabled() const { return network_capture_.enabled(); }
  void SetCaptureEnabled(bool enabled) { network_capture_.SetEnabled(enabled); }

  // CefClient
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefDisplayHandler
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  // CefLoadHandler
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;

  // CefRequestHandler
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

  // CefResourceRequestHandler
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;
  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response) override;
  void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              CefRefPtr<CefResponse> response,
                              URLRequestStatus status,
                              int64_t received_content_length) override;

 public:
  // 响应过滤器和 ClientHandler 共享的流式缓冲状态。
  struct ResponseState {
    std::string body;
    size_t limit = 0;
    bool truncated = false;
  };

 private:
  CefRefPtr<CefBrowser> GetBrowser() const;
  bool CreateBrowserOnUI(HWND parent, const RECT& bounds, const std::string& url);
  void StartSchedulerOnce();
  void StopScheduler();
  void NotifyBrowserClosed();
  static std::string MakeErrorPage(const CefString& failed_url,
                                   ErrorCode error_code,
                                   const CefString& error_text);

  HWND main_hwnd_ = nullptr;
  DWORD main_thread_id_ = 0;
  std::atomic<bool> is_closing_{false};
  std::atomic<bool> browser_creation_pending_{false};
  std::atomic<bool> close_requested_{false};
  std::atomic<bool> force_close_requested_{false};
  std::atomic<bool> close_notified_{false};
  std::shared_ptr<Config> config_;

  mutable std::mutex browser_mutex_;
  CefRefPtr<CefBrowser> browser_;

  std::mutex response_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<ResponseState>> responses_;

  NetworkCapture network_capture_;
  MarketDataEngine market_data_;
  MailService mail_service_;
  std::mutex scheduler_mutex_;
  std::unique_ptr<Scheduler> scheduler_;
  std::once_flag scheduler_once_;

  IMPLEMENT_REFCOUNTING(ClientHandler);
  DISALLOW_COPY_AND_ASSIGN(ClientHandler);
};
