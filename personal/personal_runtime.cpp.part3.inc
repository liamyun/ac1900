  // 不在 CEF UI thread 上 join 后台 Scheduler。真正退出时由
  // cefclient_win.cc 在官方主消息循环结束后统一收尾。
}

void Runtime::StartSchedulerIfNeeded() {
  if (!config_ || !market_data_ || !mail_service_) {
    return;
  }
  const bool schedule = config_->GetBool(L"schedule", L"enabled", false);
  const bool mail = mail_service_->enabled();
  if (!schedule && !mail) {
    return;
  }
  if (!scheduler_) {
    scheduler_ = std::make_unique<Scheduler>(this, config_, market_data_.get(),
                                             mail_service_.get());
    scheduler_->Start();
  }
}

void Runtime::StopScheduler() {
  if (scheduler_) {
    scheduler_->Stop();
    scheduler_.reset();
  }
}

bool Runtime::IsChatGptFamilyUrl(const std::string& raw_url) const {
  const std::string url = LowerAscii(raw_url);
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) {
    return false;
  }
  const size_t begin = scheme + 3;
  const size_t end = url.find_first_of("/?#", begin);
  std::string authority = url.substr(
      begin, end == std::string::npos ? std::string::npos : end - begin);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority.erase(0, at + 1);
  }
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    authority.resize(colon);
  }
  return HostMatches(authority, "chatgpt.com") ||
         HostMatches(authority, "openai.com") ||
         HostMatches(authority, "oaistatic.com") ||
         HostMatches(authority, "oaiusercontent.com");
}

void Runtime::OnBeforeResourceLoad(CefRefPtr<CefRequest> request) {
  CEF_REQUIRE_IO_THREAD();
  EnsureInitialized();
  if (!request || !network_capture_) {
    return;
  }

  if (config_ && config_->GetBool(L"diagnostics", L"network_log", true)) {
    const std::string raw_url = request->GetURL().ToString();
    if (IsNetworkDiagnosticUrl(raw_url)) {
      std::ostringstream diag;
      diag << "REQUEST id=" << request->GetIdentifier()
           << " method=" << request->GetMethod().ToString()
           << " type=" << static_cast<int>(request->GetResourceType())
           << " transition=" << TransitionToString(request->GetTransitionType())
           << " referrer=" << SanitizeDiagnosticUrl(request->GetReferrerURL().ToString())
           << HeaderSummary(request)
           << " url=" << SanitizeDiagnosticUrl(raw_url);
      AppendNetworkDiagnostic(diag.str());
    }
  }

  if (network_capture_->ShouldTrackRequest(request)) {
    network_capture_->CaptureRequest(request);
  }
}

CefRefPtr<CefResponseFilter> Runtime::GetResponseFilter(
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response) {
  CEF_REQUIRE_IO_THREAD();
  EnsureInitialized();
  if (!request || !response || !network_capture_ || !market_data_) {
    return nullptr;
  }

  const std::string url = request->GetURL().ToString();
  const bool market = MarketDataEngine::IsMarketApiUrl(url);

  // 可选安全阀。默认 r8=0：采集开时允许抓 ChatGPT XHR；采集默认仍关闭。
  if (!market &&
      config_->GetBool(L"capture", L"skip_chatgpt", false) &&
      IsChatGptFamilyUrl(url)) {
    return nullptr;
  }

  const bool track = network_capture_->ShouldTrackRequest(request);
  const bool capture = track && network_capture_->ShouldCaptureTextResponse(
                                  url, response->GetMimeType().ToString(),
                                  request->GetResourceType());
  if (!market && !capture) {
    return nullptr;
  }

  auto state = std::make_shared<ResponseState>();
  state->limit = market
                     ? std::max<size_t>(16 * 1024 * 1024,
                                        network_capture_->max_body_bytes())
                     : network_capture_->max_body_bytes();

  {
    std::lock_guard lock(response_mutex_);
    responses_[request->GetIdentifier()] = state;
  }
  return new PassThroughCaptureFilter(std::move(state));
}

void Runtime::OnResourceLoadComplete(CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefResponse> response,
                                     cef_urlrequest_status_t status,
                                     int64_t received_content_length) {
  CEF_REQUIRE_IO_THREAD();

  // Log completion independently of the optional capture pipeline. This is
  // important because capture is normally OFF; diagnostics must still show
  // failed OpenAI/Stripe requests, WebSocket-type resources and HTTP codes.
  if (request && config_ &&
      config_->GetBool(L"diagnostics", L"network_log", true)) {
    const std::string raw_url = request->GetURL().ToString();
    if (IsNetworkDiagnosticUrl(raw_url)) {
      std::ostringstream diag;
      diag << "COMPLETE id=" << request->GetIdentifier()
           << " method=" << request->GetMethod().ToString()
           << " type=" << static_cast<int>(request->GetResourceType())
           << " url_request_status=" << static_cast<int>(status)
           << " http=" << (response ? response->GetStatus() : 0)
           << " status_text=" << (response ? response->GetStatusText().ToString() : std::string())
           << " received=" << received_content_length
           << ResponseHeaderSummary(response)
           << " url=" << SanitizeDiagnosticUrl(raw_url);
      AppendNetworkDiagnostic(diag.str());
    }
  }

  if (!request || !response || !network_capture_ || !market_data_) {
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
  if (MarketDataEngine::IsMarketApiUrl(url) && !state->truncated) {
    market_data_->HandleResponse(url, state->body);
  }

  network_capture_->CaptureResponse(request, response, std::move(state->body),
                                    status, received_content_length,
                                    state->truncated);
}

scoped_refptr<RootWindow> Runtime::OpenHiddenMarketWindow(
    const std::string& url) {
  EnsureInitialized();
  auto config = std::make_unique<RootWindowConfig>();
  config->with_controls = false;
  config->initially_hidden = true;
  config->url = url;
  return MainContext::Get()->GetRootWindowManager()->CreateRootWindow(
      std::move(config));
}

void Runtime::CloseHiddenWindow(scoped_refptr<RootWindow> window) {
  if (!window) {
    return;
  }
  if (CURRENTLY_ON_MAIN_THREAD()) {
    if (window->IsInitialized()) {
      window->Close(false);
    }
    return;
  }
  MAIN_POST_CLOSURE(base::BindOnce(
      [](scoped_refptr<RootWindow> root) {
        if (root && root->IsInitialized()) {
          root->Close(false);
        }
      },
      std::move(window)));
}

bool Runtime::capture_enabled() const {
  return network_capture_ && network_capture_->enabled();
}

void Runtime::SetCaptureEnabled(bool enabled) {
  EnsureInitialized();
  if (network_capture_) {
    network_capture_->SetEnabled(enabled);
  }
  if (config_) {
    config_->SetBool(L"capture", L"enabled", enabled);
  }
}

std::string Runtime::theme() const {
  if (!config_) {
    return "system";
  }
  return LowerAscii(config_->GetString(L"ui", L"theme", "system"));
}

void Runtime::SetTheme(const std::string& value) {
  EnsureInitialized();
  const std::string normalized = LowerAscii(value);
  if (normalized != "system" && normalized != "light" &&
      normalized != "dark") {
    return;
  }
  config_->SetWString(L"ui", L"theme", util::Utf8ToWide(normalized));
}

std::filesystem::path Runtime::config_path() const {
  return config_ ? config_->path() : util::ExeDir() / L"set.ini";
}

std::filesystem::path Runtime::data_root() const {
  return market_data_ ? market_data_->data_root() : util::ExeDir();
}

std::filesystem::path Runtime::capture_dir() const {
  if (!config_) {
    return util::ExeDir() / L"capture";
  }
  return config_->GetPath(L"capture", L"directory", util::ExeDir() / L"capture");
}

}  // namespace client::personal
