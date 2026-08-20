# AI Handoff — Read Before Modifying PersonalCEF

This file exists to prevent future AI agents from repeating work that has already been done.

## Project objective

PersonalCEF is a **thin Windows shell around CEF/Chromium**. Its value comes from *not* replacing Chromium features that already work well.

The primary architecture rule is:

> **Let Chromium own tabs, browser UI, navigation, renderer/network processes and browser behavior whenever possible. Add only a thin outer integration layer.**

If a proposed change requires rebuilding Chromium tabs, manually reparenting browser views, inventing a second window framework, or moving core browser behavior into custom Win32 code, assume the design is wrong until proven otherwise.

## Known-good baseline

- CEF: `151.3.16+gbe1e15d+chromium-151.0.7922.109_windows64`
- CEF commit: `be1e15d`
- Platform: Windows x64
- Toolchain: Visual Studio 2022 + CMake
- Main UI mode: **CEF Chrome runtime style** (`CEF_RUNTIME_STYLE_CHROME`)
- Main browser creation: `CefBrowserHost::CreateBrowser(...)`
- Native Chromium tabs/windows are preferred over custom tab frameworks.
- Persistent profile/cache is per application.
- SOCKS5 proxy, if enabled, is configured on the Chromium command line **before `CefInitialize`**.

## Source-tree rule

There should be one authoritative PersonalCEF customization tree:

```text
personal/
```

The official CEF SDK / cefclient source should be treated as upstream input. Build/sync scripts may copy or patch official files as part of generation, but humans and AI agents should not maintain two competing editable PersonalCEF source trees.

## Main-window architecture

Earlier builds wrapped the main browser in cefclient `RootWindow` / `ViewsWindow`. That worked for ordinary cefclient behavior but did not provide the same native Chrome-style tab experience.

The current direction creates the main window as an unmanaged Chrome-style CEF browser:

```cpp
CefWindowInfo window_info;
window_info.runtime_style = CEF_RUNTIME_STYLE_CHROME;

CefBrowserHost::CreateBrowser(
    window_info,
    PersonalChromeHandler::GetShared(),
    startup_url,
    browser_settings,
    nullptr,
    nullptr);
```

`GetDefaultClient()` must return the PersonalCEF Chrome handler for Chrome-style windows created by Chromium.

## Proxy rules

When SOCKS is enabled:

- Set `proxy-server=socks5://HOST:PORT` before `CefInitialize`.
- Remove conflicting proxy switches before applying the desired one.
- Prefer deterministic replacement over repeated appending.
- SOCKS5 DNS can be forced with Chromium `host-resolver-rules` when appropriate.
- QUIC should normally be disabled for a SOCKS-only path because QUIC is UDP and SOCKS5 proxying here is intended for Chromium TCP URL traffic.
- Do not infer "proxy failure" merely because a site API returns HTTP 403. HTTP 403 means the request reached a server and was refused at the HTTP/application/security layer.

## Windows integration

Single-instance behavior, tray integration, icon handling and login-helper behavior belong in the thin Windows layer. They should not replace Chromium window/tab ownership.

When implementing close-to-tray, remember that Chrome-style windows may process close operations at multiple levels. Previous work needed interception at more than one layer, including Win32 close messages and the Chrome command path.

## Diagnostic philosophy

Do not guess when a reproducible failure can be instrumented.

For network bugs, log at least:

- effective Chromium proxy command-line switches
- navigation transitions (including reload)
- URL, method and resource type
- HTTP status
- CEF URL request status
- selected response headers useful for diagnosis

Do **not** log secrets such as Cookie values, Authorization values or full sensitive response bodies. Presence/length is usually sufficient.

## ChatGPT-specific observation

During testing, ChatGPT could load static resources successfully while some `/backend-api/...` requests returned HTTP 403. Later requests could return 200 after another navigation/reload. This is evidence that "page loads" and "authenticated backend state is ready" are separate conditions.

Do not respond to that symptom by randomly changing SOCKS parameters if the diagnostics already show Chromium is using the configured proxy and HTTP responses are arriving.

## Things future AI agents must not casually change

1. Do not replace native Chrome tabs with a custom tab bar.
2. Do not create a second editable PersonalCEF source tree under copied cefclient sources.
3. Do not change working login-helper behavior while adding unrelated features.
4. Do not change tray/single-instance behavior while debugging a network-only problem unless evidence connects them.
5. Do not write user-specific proxy addresses, absolute paths, cookies, cache or logs into public defaults.
6. Do not claim a Windows build succeeded unless it was actually compiled/tested on Windows.
7. Do not assume Chrome has a permanent status bar; desktop Chrome does not.

## How to work on this repository

Before a change:

1. Read `docs/LESSONS_LEARNED.md`.
2. Identify the smallest layer that owns the behavior.
3. Preserve already-working behavior outside that layer.
4. Add diagnostics before architectural rewrites.
5. Keep the patch reversible.

After a change:

1. Record what changed and why.
2. Record what was tested versus what was only statically inspected.
3. Add newly discovered dead ends to `docs/LESSONS_LEARNED.md`.

The purpose of this file is continuity. Treat it as part of the source code, not optional prose.
