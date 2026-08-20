#pragma once

#include "tests/cefclient/browser/default_client_handler.h"
#include "include/cef_command_handler.h"

namespace client::personal {

// PersonalCEF r11 只扩展浏览器级回调；窗口/标签栏/地址栏全部交给
// CEF Chrome runtime 原生实现，不再自己创建或管理 Tab UI。
class PersonalChromeHandler final : public DefaultClientHandler,
                                    public CefCommandHandler {
 public:
  static CefRefPtr<PersonalChromeHandler> GetShared();

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;

  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  bool OnChromeCommand(CefRefPtr<CefBrowser> browser,
                       int command_id,
                       cef_window_open_disposition_t disposition) override;

  cef_return_value_t OnBeforeResourceLoad(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefCallback> callback) override;
  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response) override;
  void OnResourceLoadComplete(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response,
      CefResourceRequestHandler::URLRequestStatus status,
      int64_t received_content_length) override;

 private:
  PersonalChromeHandler();
  IMPLEMENT_REFCOUNTING(PersonalChromeHandler);
};

}  // namespace client::personal
