#include "client_handler.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "include/cef_browser.h"
#include "include/cef_callback.h"
#include "include/cef_frame.h"
#include "include/cef_response_filter.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "config.h"
#include "scheduler.h"
#include "ui_messages.h"
#include "util.h"

namespace {

class PassThroughCaptureFilter : public CefResponseFilter {
 public:
  explicit PassThroughCaptureFilter(
      std::shared_ptr<ClientHandler::ResponseState> state)
      : state_(std::move(state)) {}

  bool InitFilter() override {
    return state_ != nullptr;
  }

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override {
    data_in_read = 0;
    data_out_written = 0;

    if (!data_out || data_out_size == 0) {
      return RESPONSE_FILTER_NEED_MORE_DATA;
    }
    if (!data_in || data_in_size == 0) {
      return RESPONSE_FILTER_DONE;
    }

    // 旧工程在 data_out_size < data_in_size 时仍把整个输入标成已读，
    // 会悄悄丢掉尾部。这里严格只消费实际复制出去的字节。
    const size_t n = std::min(data_in_size, data_out_size);
    std::memcpy(data_out, data_in, n);
    data_in_read = n;
    data_out_written = n;

    if (state_->body.size() < state_->limit) {
      const size_t remaining = state_->limit - state_->body.size();
      const size_t append = std::min(n, remaining);
      state_->body.append(static_cast<const char*>(data_in), append);
      if (append < n) {
        state_->truncated = true;
      }
    } else {
      state_->truncated = true;
    }

    return n == data_in_size ? RESPONSE_FILTER_DONE
                             : RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  std::shared_ptr<ClientHandler::ResponseState> state_;

  IMPLEMENT_REFCOUNTING(PassThroughCaptureFilter);
  DISALLOW_COPY_AND_ASSIGN(PassThroughCaptureFilter);
};

void PostWString(HWND hwnd, UINT message, const std::wstring& value) {
  auto* copy = new std::wstring(value);
  if (!::PostMessageW(hwnd, message, 0,
                      reinterpret_cast<LPARAM>(copy))) {
    delete copy;
  }
}

}  // namespace

ClientHandler::ClientHandler(HWND main_hwnd,
                             std::shared_ptr<Config> config)
    : main_hwnd_(main_hwnd),
      main_thread_id_(::GetCurrentThreadId()),
      config_(std::move(config)),
      network_capture_(config_),
      market_data_(config_),
      mail_service_(config_) {}

ClientHandler::~ClientHandler() {
  StopScheduler();
}

bool ClientHandler::CreateBrowser(HWND parent,
                                  const RECT& bounds,
                                  const std::string& url) {
  browser_creation_pending_.store(true);
  if (CefCurrentlyOn(TID_UI)) {
    return CreateBrowserOnUI(parent, bounds, url);
  }
  CefRefPtr<ClientHandler> self(this);
  const bool posted = CefPostTask(
      TID_UI, base::BindOnce(
                  [](CefRefPtr<ClientHandler> handler, HWND parent_hwnd,
                     RECT browser_bounds, std::string browser_url) {
                    handler->CreateBrowserOnUI(
                        parent_hwnd, browser_bounds, browser_url);
                  },
                  self, parent, bounds, url));
  if (!posted) {
    browser_creation_pending_.store(false);
  }
  return posted;
}

bool ClientHandler::CreateBrowserOnUI(HWND parent,
                                      const RECT& bounds,
                                      const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (close_requested_.load()) {
    browser_creation_pending_.store(false);
    NotifyBrowserClosed();
    return false;
  }

  CefWindowInfo window_info;
  const int width = std::max<LONG>(1, bounds.right - bounds.left);
  const int height = std::max<LONG>(1, bounds.bottom - bounds.top);
  window_info.SetAsChild(parent,
                         CefRect(bounds.left, bounds.top, width, height));
  // 强制 Alloy/native child 模式，不让新版 CEF 自己生成一套 Chrome 顶层 UI。
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

  CefBrowserSettings browser_settings;
  const bool started = CefBrowserHost::CreateBrowser(
      window_info, this, url, browser_settings, nullptr, nullptr);
  if (!started) {
    browser_creation_pending_.store(false);
    NotifyBrowserClosed();
  }
  return started;
}

CefRefPtr<CefBrowser> ClientHandler::GetBrowser() const {
  std::lock_guard lock(browser_mutex_);
  return browser_;
}

void ClientHandler::Navigate(const std::string& url) {
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler, std::string target) {
          handler->Navigate(target);
        }, self, url));
    return;
  }
  CefRefPtr<CefBrowser> browser = GetBrowser();
  if (browser && browser->GetMainFrame()) {
    browser->GetMainFrame()->LoadURL(url);
  }
}

void ClientHandler::GoBack() {
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler) { handler->GoBack(); }, self));
    return;
  }
  if (auto browser = GetBrowser(); browser && browser->CanGoBack()) {
    browser->GoBack();
  }
}

void ClientHandler::GoForward() {
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler) { handler->GoForward(); }, self));
    return;
  }
  if (auto browser = GetBrowser(); browser && browser->CanGoForward()) {
    browser->GoForward();
  }
}

void ClientHandler::Reload() {
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler) { handler->Reload(); }, self));
    return;
  }
  if (auto browser = GetBrowser()) {
    browser->Reload();
  }
}

void ClientHandler::StopLoad() {
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler) { handler->StopLoad(); }, self));
    return;
  }
  if (auto browser = GetBrowser()) {
    browser->StopLoad();
  }
}

void ClientHandler::CloseBrowser(bool force_close) {
  close_requested_.store(true);
  if (force_close) {
    force_close_requested_.store(true);
  }
  StopScheduler();
  if (!CefCurrentlyOn(TID_UI)) {
    CefRefPtr<ClientHandler> self(this);
    CefPostTask(TID_UI, base::BindOnce(
        [](CefRefPtr<ClientHandler> handler, bool force) {
          handler->CloseBrowser(force);
        }, self, force_close));
    return;
  }
  if (auto browser = GetBrowser()) {
    browser->GetHost()->CloseBrowser(force_close_requested_.load());
  } else if (!browser_creation_pending_.load()) {
    NotifyBrowserClosed();
  }
}

void ClientHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (frame && frame->IsMain()) {
    PostWString(main_hwnd_, WM_APP_ADDRESS_CHANGED, url.ToWString());
  }
}

void ClientHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  PostWString(main_hwnd_, WM_APP_TITLE_CHANGED, title.ToWString());
}

void ClientHandler::StartSchedulerOnce() {
  std::call_once(scheduler_once_, [this] {
    std::lock_guard lock(scheduler_mutex_);
    scheduler_ = std::make_unique<Scheduler>(
        this, config_, &market_data_, &mail_service_);
    scheduler_->Start();
  });
}

void ClientHandler::StopScheduler() {
  // Start/Stop 可能分别来自 CEF UI 线程和 Win32 主线程，所有权操作串行化。
  std::lock_guard lock(scheduler_mutex_);
  if (scheduler_) {
    scheduler_->Stop();
  }
}

void ClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  browser_creation_pending_.store(false);
  {
    std::lock_guard lock(browser_mutex_);
    if (!browser_) {
      browser_ = browser;
    }
  }

  if (close_requested_.load()) {
    browser->GetHost()->CloseBrowser(force_close_requested_.load());
    return;
  }

  const HWND browser_hwnd = browser->GetHost()->GetWindowHandle();
  ::PostMessageW(main_hwnd_, WM_APP_BROWSER_CREATED,
                 reinterpret_cast<WPARAM>(browser_hwnd), 0);
  StartSchedulerOnce();
}

bool ClientHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // 告诉 Win32 顶层窗口：这次 WM_CLOSE 是 CEF 真正的关闭流程，
  // 不要再按“关闭到托盘”处理。
  is_closing_.store(true);
  return false;
}

void ClientHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  StopScheduler();
  {
    std::lock_guard lock(browser_mutex_);
    if (browser_ && browser_->IsSame(browser)) {
      browser_ = nullptr;
    }
  }
  NotifyBrowserClosed();
}

void ClientHandler::NotifyBrowserClosed() {
  if (close_notified_.exchange(true)) {
    return;
  }
  if (!::PostMessageW(main_hwnd_, WM_APP_BROWSER_CLOSED, 0, 0)) {
    ::PostThreadMessageW(main_thread_id_, WM_APP_BROWSER_CLOSED, 0, 0);
  }
}

bool ClientHandler::OnBeforePopup(
    CefRefPtr<CefBrowser> browser,
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
    bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();

  // 个人工具保持单窗口：target=_blank / window.open 不创建第二个 CEF 顶层窗。
  // 有明确目标 URL 时改为当前页导航；about:blank/空目标也直接拦掉 popup。
  if (!target_url.empty() && browser && browser->GetMainFrame()) {
    browser->GetMainFrame()->LoadURL(target_url);
  }
  return true;
}

void ClientHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool is_loading,
                                         bool can_go_back,
                                         bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  LPARAM flags = 0;
  if (can_go_back) flags |= 1;
  if (can_go_forward) flags |= 2;
  ::PostMessageW(main_hwnd_, WM_APP_LOADING_CHANGED,
                 is_loading ? 1 : 0, flags);
}

std::string ClientHandler::MakeErrorPage(const CefString& failed_url,
                                         ErrorCode error_code,
                                         const CefString& error_text) {
  std::ostringstream html;
  html << "<!doctype html><meta charset=utf-8>"
       << "<style>body{font-family:Segoe UI,Microsoft YaHei,sans-serif;"
       << "padding:42px;color:#242424;background:#fafafa}"
       << "code{background:#eee;padding:2px 5px;border-radius:4px}</style>"
       << "<h2>页面加载失败</h2><p><code>"
       << util::HtmlEscape(failed_url.ToString())
       << "</code></p><p>CEF error " << static_cast<int>(error_code)
       << "： " << util::HtmlEscape(error_text.ToString()) << "</p>";
  return html.str();
}

void ClientHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode error_code,
                                const CefString& error_text,
                                const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || !frame->IsMain() || error_code == ERR_ABORTED) {
    return;
  }

  const std::string html = MakeErrorPage(failed_url, error_code, error_text);
  const std::string data_url =
      "data:text/html;charset=utf-8;base64," + util::Base64Encode(html);
  frame->LoadURL(data_url);
}

CefRefPtr<CefResourceRequestHandler>
ClientHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  CEF_REQUIRE_IO_THREAD();
  disable_default_handling = false;

  // ChatGPT 专用模式且采集关闭时，不给普通网页请求挂资源回调，
  // 尽量保持与普通 CEF 浏览器相同的网络开销。东财 API 仍保留原处理链。
  if (!request) {
    return nullptr;
  }
  const std::string url = request->GetURL().ToString();
  const bool market = MarketDataEngine::IsMarketApiUrl(url);
  if (!market && !network_capture_.ShouldTrackRequest(request)) {
    return nullptr;
  }

  // 返回 this 后，CEF 151 会把后续请求/响应回调交给
  // CefResourceRequestHandler，而不是旧版直接塞在 CefRequestHandler 里。
  return this;
}

CefResourceRequestHandler::ReturnValue
ClientHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_IO_THREAD();
  network_capture_.CaptureRequest(request);
  return RV_CONTINUE;
}

CefRefPtr<CefResponseFilter>
ClientHandler::GetResourceResponseFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response) {
  CEF_REQUIRE_IO_THREAD();

  if (!request || !response) {
    return nullptr;
  }

  const std::string url = request->GetURL().ToString();
  const bool market = MarketDataEngine::IsMarketApiUrl(url);
  const bool network = network_capture_.ShouldCaptureTextResponse(
      url, response->GetMimeType().ToString(), request->GetResourceType());
  if (!market && !network) {
    return nullptr;
  }

  auto state = std::make_shared<ResponseState>();
  // 东财趋势数据是核心功能，即使用户把通用 capture 限制得很小，
  // 也给市场接口至少 16 MiB，防止报告数据被截断。
  state->limit = market
      ? std::max<size_t>(16 * 1024 * 1024,
                         network_capture_.max_body_bytes())
      : network_capture_.max_body_bytes();

  {
    std::lock_guard lock(response_mutex_);
    responses_[request->GetIdentifier()] = state;
  }
  return new PassThroughCaptureFilter(std::move(state));
}

void ClientHandler::OnResourceLoadComplete(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response,
    URLRequestStatus status,
    int64_t received_content_length) {
  CEF_REQUIRE_IO_THREAD();
  if (!request || !response) {
    return;
  }

  std::shared_ptr<ResponseState> state;
  {
    std::lock_guard lock(response_mutex_);
    const auto it = responses_.find(request->GetIdentifier());
    if (it != responses_.end()) {
      state = std::move(it->second);
      responses_.erase(it);
    }
  }
  if (!state) {
    return;
  }

  const std::string url = request->GetURL().ToString();

  // 市场数据先处理再 move 给后台采集队列，避免正文被 move 后变空。
  if (MarketDataEngine::IsMarketApiUrl(url) && !state->truncated) {
    market_data_.HandleResponse(url, state->body);
  }

  network_capture_.CaptureResponse(
      request, response, std::move(state->body), status,
      received_content_length, state->truncated);
}
