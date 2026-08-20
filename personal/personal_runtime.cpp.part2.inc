    disabled.push_back(',');
  }
  disabled += feature;

  // CEF 151 supports RemoveSwitch(). Replace the existing switch so we don't
  // depend on duplicate-switch parsing order.
  if (command_line->HasSwitch("disable-features")) {
    command_line->RemoveSwitch("disable-features");
  }
  command_line->AppendSwitchWithValue("disable-features", disabled);
}

}  // namespace

Runtime& Runtime::Get() {
  static Runtime instance;
  return instance;
}

Runtime::Runtime() = default;
Runtime::~Runtime() { Shutdown(); }

void Runtime::EnsureConfig() {
  std::lock_guard lock(init_mutex_);
  if (!config_) {
    config_ = std::make_shared<Config>(util::ExeDir() / L"set.ini");
  }
}

void Runtime::EnsureInitialized() {
  std::lock_guard lock(init_mutex_);
  if (initialized_) {
    return;
  }
  if (!config_) {
    config_ = std::make_shared<Config>(util::ExeDir() / L"set.ini");
  }

  // 这些对象可能创建后台 writer 线程，所以等 Browser 真正创建时再初始化，
  // 不在 CEF 进程/消息循环尚未就绪的 command-line 阶段启动它们。
  network_capture_ = std::make_unique<NetworkCapture>(config_);
  market_data_ = std::make_unique<MarketDataEngine>(config_);
  mail_service_ = std::make_unique<MailService>(config_);
  initialized_ = true;
}

void Runtime::ConfigureCommandLine(CefRefPtr<CefCommandLine> command_line) {
  EnsureConfig();
  if (!command_line) {
    return;
  }

  // 默认 ChatGPT，但仍尊重用户显式传入的 --url。
  if (!command_line->HasSwitch("url")) {
    command_line->AppendSwitchWithValue(
        "url", config_->GetString(L"browser", L"home_url",
                                  "https://chatgpt.com/"));
  }

  // 使用官方 Views + Chrome toolbar；隐藏 cefclient 测试用顶层菜单。
  if (!command_line->HasSwitch("hide-top-menu")) {
    command_line->AppendSwitch("hide-top-menu");
  }

  // Chromium/CEF 原生界面语言。--lang 控制 Chrome runtime UI/资源语言；
  // --accept-lang 控制 Accept-Language 和 navigator.language。
  const std::string language =
      config_->GetString(L"browser", L"language", "zh-CN");
  if (!language.empty() && !command_line->HasSwitch("lang")) {
    command_line->AppendSwitchWithValue("lang", language);
  }

  const std::string accept_language = config_->GetString(
      L"browser", L"accept_language", "zh-CN,zh,en-US,en");
  if (!accept_language.empty() && !command_line->HasSwitch("accept-lang")) {
    command_line->AppendSwitchWithValue("accept-lang", accept_language);
  }

  // 只关闭 WebAuthn 的“条件式 Passkey 自动下拉 UI”，不关闭普通密码登录，
  // 也不彻底禁用 WebAuthentication。需要 Passkey 时可在 set.ini 改为 0。
  if (config_->GetBool(L"browser", L"disable_passkey_ui", true)) {
    AddDisabledFeature(command_line, "WebAuthenticationConditionalUI");
  }

  // 持久化 ChatGPT 登录/cookie/cache。
  if (!command_line->HasSwitch("cache-path")) {
    const auto cache = util::ExeDir() / L"runtime" / L"cef-cache";
    util::EnsureDir(cache);
    command_line->AppendSwitchWithValue("cache-path", util::WideToUtf8(cache.wstring()));
  }

  // r11.3: SOCKS must be committed to Chromium before the first browser is
  // created. ConfigureCommandLine is called once on the local command line
  // before CefInitialize and again from cefclient's official
  // OnBeforeCommandLineProcessing hook. Replace conflicting switches instead
  // of appending duplicates, so the effective proxy is deterministic.
  const bool proxy_enabled = config_->GetBool(L"proxy", L"enabled", true);
  if (proxy_enabled) {
    std::string type =
        LowerAscii(config_->GetString(L"proxy", L"type", "socks5"));
    if (type != "socks5" && type != "socks" && type != "socks4" &&
        type != "http" && type != "https") {
      type = "socks5";
    }
    const std::string host =
        config_->GetString(L"proxy", L"host", "127.0.0.1");
    const int port = config_->GetInt(L"proxy", L"port", 1081);

    if (!host.empty() && port > 0) {
      // Remove switches that can override or conflict with manual proxy mode.
      command_line->RemoveSwitch("no-proxy-server");
      command_line->RemoveSwitch("proxy-auto-detect");
      command_line->RemoveSwitch("proxy-pac-url");

      const std::string proxy =
          type + "://" + host + ":" + std::to_string(port);
      ReplaceSwitch(command_line, "proxy-server", proxy);

      const std::string bypass = config_->GetString(
          L"proxy", L"bypass_list", "<local>;localhost;127.0.0.1");
      if (!bypass.empty()) {
        ReplaceSwitch(command_line, "proxy-bypass-list", bypass);
      } else {
        command_line->RemoveSwitch("proxy-bypass-list");
      }

      // Chrome SOCKS5 resolves URL hostnames at the SOCKS server. This extra
      // resolver rule also prevents non-URL Chromium components from leaking
      // ordinary hostname lookups directly. Numeric/local endpoints remain
      // usable. Can be disabled in set.ini if a special LAN workflow needs it.
      if (config_->GetBool(L"proxy", L"force_remote_dns", true) &&
          (type == "socks5" || type == "socks")) {
        const std::string resolver_rules =
            "MAP * ~NOTFOUND, EXCLUDE " + host +
            ", EXCLUDE localhost, EXCLUDE 127.0.0.1, EXCLUDE [::1]";
        ReplaceSwitch(command_line, "host-resolver-rules", resolver_rules);
      }
    }
  }

  if (proxy_enabled && config_->GetBool(L"proxy", L"disable_quic", true)) {
    ReplaceSwitch(command_line, "disable-quic", std::string());
  }

  // r11.4: network diagnostics only. This does not alter page UI, tabs, tray,
  // login flow, capture state or response bodies. It records the *effective*
  // Chromium network switches at both configuration points so we can verify
  // that the browser process actually starts with SOCKS before the first page.
  if (config_->GetBool(L"diagnostics", L"network_log", true)) {
    static int configure_call = 0;
    const int call = ++configure_call;
    std::ostringstream diag;
    if (call == 1) {
      AppendNetworkDiagnostic(
          "=== PersonalCEF r11.5 precise network diagnostic session begin ===");
    }
    diag << "CONFIG call=" << call
         << " proxy-server=" << EffectiveSwitch(command_line, "proxy-server")
         << " proxy-bypass-list="
         << EffectiveSwitch(command_line, "proxy-bypass-list")
         << " host-resolver-rules="
         << EffectiveSwitch(command_line, "host-resolver-rules")
         << " disable-quic=" << EffectiveSwitch(command_line, "disable-quic")
         << " lang=" << EffectiveSwitch(command_line, "lang")
         << " accept-lang=" << EffectiveSwitch(command_line, "accept-lang")
         << " cache-path=" << EffectiveSwitch(command_line, "cache-path");
    AppendNetworkDiagnostic(diag.str());
  }

  const int debugging_port =
      config_->GetInt(L"browser", L"remote_debugging_port", 0);
  if (debugging_port > 0 &&
      !command_line->HasSwitch("remote-debugging-port")) {
    command_line->AppendSwitchWithValue("remote-debugging-port",
                                        std::to_string(debugging_port));
  }
}

void Runtime::PrepareForCefShutdown() {
  // 此时官方 RootWindowManager 已经结束主消息循环。不要再启动新的 Browser。
  StopScheduler();
}

void Runtime::Shutdown() {
  {
    // 这里只保护状态切换，不能拿着 init_mutex_ 去 join 后台线程。
    // Scheduler 在退出过程中仍可能经过 Runtime 的公开方法。
    std::lock_guard lock(init_mutex_);
    if (shutdown_ || (!initialized_ && !config_)) {
      return;
    }
    shutdown_ = true;
  }

  StopScheduler();
  {
    std::lock_guard browser_lock(browser_mutex_);
    main_browser_id_ = 0;
  }
  {
    std::lock_guard lock(response_mutex_);
    responses_.clear();
  }

  mail_service_.reset();
  market_data_.reset();
  network_capture_.reset();
  config_.reset();
}


void Runtime::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             CefRefPtr<CefRequest> request,
                             bool user_gesture,
                             bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  EnsureConfig();
  if (!request || !config_ ||
      !config_->GetBool(L"diagnostics", L"network_log", true)) {
    return;
  }
  const std::string raw_url = request->GetURL().ToString();
  if (!IsNetworkDiagnosticUrl(raw_url)) {
    return;
  }
  std::ostringstream diag;
  diag << "NAV browser=" << (browser ? browser->GetIdentifier() : 0)
       << " frame=" << ((frame && frame->IsMain()) ? "main" : "sub")
       << " user_gesture=" << (user_gesture ? 1 : 0)
       << " redirect=" << (is_redirect ? 1 : 0)
       << " transition=" << TransitionToString(request->GetTransitionType())
       << " method=" << request->GetMethod().ToString()
       << " url=" << SanitizeDiagnosticUrl(raw_url);
  AppendNetworkDiagnostic(diag.str());
}

void Runtime::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  EnsureInitialized();
  if (!browser) {
    return;
  }

  if (config_ && config_->GetBool(L"diagnostics", L"network_log", true)) {
    std::ostringstream diag;
    diag << "BROWSER_CREATED id=" << browser->GetIdentifier()
         << " opener=" << browser->GetHost()->GetOpenerIdentifier();
    AppendNetworkDiagnostic(diag.str());
  }

  // 第一个无 opener 的 Browser 是主 ChatGPT Browser。
  // 定时行情窗口在主窗口之后创建，不会抢占。
  if (browser->GetHost()->GetOpenerIdentifier() <= 0) {
    bool became_main = false;
    {
      std::lock_guard lock(browser_mutex_);
      if (main_browser_id_ == 0) {
        main_browser_id_ = browser->GetIdentifier();
        became_main = true;
      }
    }
    if (became_main) {
      StartSchedulerIfNeeded();
    }
  }
}

void Runtime::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    return;
  }

  {
    std::lock_guard lock(browser_mutex_);
    if (main_browser_id_ == browser->GetIdentifier()) {
      main_browser_id_ = 0;
    }
  }
