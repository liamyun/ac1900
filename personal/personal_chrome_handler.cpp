#include "personal_chrome_handler.h"

#include "include/wrapper/cef_helpers.h"
#include "include/cef_id_mappers.h"

#include "personal_runtime.h"
#include "personal_tray_win.h"

namespace client::personal {

PersonalChromeHandler::PersonalChromeHandler()
    : DefaultClientHandler(/*use_alloy_style=*/false) {}

CefRefPtr<PersonalChromeHandler> PersonalChromeHandler::GetShared() {
  static CefRefPtr<PersonalChromeHandler> handler = new PersonalChromeHandler();
  return handler;
}

void PersonalChromeHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  DefaultClientHandler::OnAfterCreated(browser);
  Runtime::Get().OnBrowserCreated(browser);
  TrayController::Get().AttachBrowser(browser);
  TrayController::Get().SetBrowserCount(GetBrowserCount());
}


bool PersonalChromeHandler::OnChromeCommand(
    CefRefPtr<CefBrowser> browser,
    int command_id,
    cef_window_open_disposition_t disposition) {
  CEF_REQUIRE_UI_THREAD();

  // Chrome-style 窗口的标题栏 X 并不可靠地经过 Win32 WM_CLOSE。
  // Chromium 会先把它转换为 IDC_CLOSE_WINDOW 命令，所以必须在
  // CefCommandHandler 这一层拦截，才能真正做到“关闭=隐藏到托盘”。
  static const int kCloseWindow =
      cef_id_for_command_id_name("IDC_CLOSE_WINDOW");
  if (kCloseWindow > 0 && command_id == kCloseWindow &&
      TrayController::Get().HandleChromeCloseCommand()) {
    return true;  // 已隐藏主窗口，禁止 Chromium 继续销毁 Browser/所有 Tab。
  }

  // 其他 Chrome 原生命令全部交还 Chromium，保持原生多标签页行为。
  return false;
}

void PersonalChromeHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Runtime::Get().OnBrowserClosed(browser);

  // GetBrowserCount() 此时还是减 1 之前的值；==1 代表这是最后一个 Tab。
  const int count_before_close = GetBrowserCount();
  const bool last_browser = count_before_close == 1;
  if (last_browser) {
    TrayController::Get().DetachBrowser(browser);
  } else {
    TrayController::Get().SetBrowserCount(count_before_close - 1);
  }
  DefaultClientHandler::OnBeforeClose(browser);
}


bool PersonalChromeHandler::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool user_gesture,
    bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  Runtime::Get().OnBeforeBrowse(browser, frame, request, user_gesture,
                                is_redirect);
  return DefaultClientHandler::OnBeforeBrowse(browser, frame, request,
                                               user_gesture, is_redirect);
}

cef_return_value_t PersonalChromeHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_IO_THREAD();
  Runtime::Get().OnBeforeResourceLoad(request);
  return DefaultClientHandler::OnBeforeResourceLoad(browser, frame, request,
                                                     callback);
}

CefRefPtr<CefResponseFilter> PersonalChromeHandler::GetResourceResponseFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response) {
  CEF_REQUIRE_IO_THREAD();
  if (auto official_filter = DefaultClientHandler::GetResourceResponseFilter(
          browser, frame, request, response)) {
    return official_filter;
  }
  return Runtime::Get().GetResponseFilter(request, response);
}

void PersonalChromeHandler::OnResourceLoadComplete(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response,
    CefResourceRequestHandler::URLRequestStatus status,
    int64_t received_content_length) {
  CEF_REQUIRE_IO_THREAD();
  Runtime::Get().OnResourceLoadComplete(request, response, status,
                                        received_content_length);
  if (response) {
    TrayController::Get().SetLastHttpStatus(response->GetStatus());
  }
}

}  // namespace client::personal
