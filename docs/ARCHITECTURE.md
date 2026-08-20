# PersonalCEF Architecture

## Goal

PersonalCEF is not intended to become a second browser engine or a large custom Chromium fork. It is a small Windows application layer around the Chromium Embedded Framework.

The architecture therefore separates responsibilities deliberately.

## Layer 1 — Upstream CEF / Chromium

CEF/Chromium owns:

- Chrome-style top-level browser window
- native tab strip and tab behavior
- renderer processes
- browser/network processes
- navigation
- JavaScript execution
- GPU/rendering pipeline
- Chromium HTTP stack
- browser security behavior

The preferred runtime style is:

```cpp
CEF_RUNTIME_STYLE_CHROME
```

The main browser is created with `CefBrowserHost::CreateBrowser(...)` rather than by rebuilding a browser shell around custom tabs.

## Layer 2 — PersonalCEF CEF integration

The `personal/` code supplies the application's CEF-facing behavior, including:

- default client/handler for Chrome-style windows
- configuration application
- resource/network diagnostics
- app-specific lifecycle hooks
- connection between CEF browser lifecycle and Windows integration

The handler used by unmanaged Chrome-style windows must be available through the CEF default-client path.

## Layer 3 — Thin Windows integration

Windows-specific behavior includes:

- application icon
- single-instance coordination
- restore/wake of an existing instance
- system tray icon/menu
- close/minimize-to-tray policy
- true Exit behavior
- launching and returning from the login helper

This layer should remain outside Chromium's responsibility and should not become a replacement browser-window framework.

## Configuration

Runtime configuration is intentionally externalized.

Public repository:

```text
set.default.ini
```

Local/private machine:

```text
set.ini
```

Private runtime configuration and state must not be committed.

Typical configuration areas include:

- startup/home URL
- language / accept-language
- persistent cache/profile path
- proxy enable/type/host/port
- proxy bypass list
- remote-DNS behavior
- QUIC disable
- optional diagnostics

## Proxy initialization

When enabled, proxy configuration must be committed to the Chromium command line before browser/network initialization.

Desired sequence:

```text
load PersonalCEF config
        ↓
normalize/remove conflicting Chromium switches
        ↓
apply proxy-server / resolver / QUIC switches
        ↓
CefInitialize
        ↓
create Chrome-style browser window
```

Do not wait until after the first browser has been created to establish the primary network route.

## Profile/cache

The application uses a dedicated persistent profile/cache directory. This is important for normal browser session behavior and should not be casually relocated between releases.

Public source must never include the contents of the runtime profile/cache.

## Build model

The intended build model is:

```text
pinned CEF version
      ↓
prepare/download official CEF SDK
      ↓
synchronize official cefclient framework
      ↓
apply PersonalCEF-specific source/patches
      ↓
generate VS2022 solution with CMake
      ↓
Debug / Release build
```

The official CEF source should remain recognizable as upstream. PersonalCEF customizations should be isolated and reproducible.

## Why native Chrome-style tabs matter

Native tabs are not just a visual choice. Allowing Chromium to own the tab/window system also preserves its mature assumptions about:

- process ownership
- focus
- navigation
- popup/new-tab behavior
- browser commands
- shutdown
- rendering

Attempts to emulate or reparent those pieces outside Chromium increase the number of states PersonalCEF must synchronize and therefore increase the probability of hangs/regressions.

## Extension philosophy

For any proposed feature, choose ownership in this order:

1. **CEF/Chromium already supports it** → configure/use the native behavior.
2. **CEF exposes a supported callback/command** → hook that minimal API.
3. **Windows shell behavior** → implement in the thin Win32 layer.
4. **Only if none of the above applies** → consider new custom UI/framework code.

This ordering is intentional and should be preserved by future maintainers and AI agents.
