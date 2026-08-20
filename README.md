# PersonalCEF

A thin Windows browser shell built on the Chromium Embedded Framework (CEF).
The project deliberately keeps Chromium's native **Chrome-style window and tabs** instead of rebuilding a tab system around CEF.

> Experimental / alpha software. Not affiliated with Google, Chromium, Spotify, or OpenAI. ChatGPT is only the default configurable home page.

## Why this exists

Many small CEF browsers become complicated because they rebuild tabs, window management and browser-process behavior that Chromium already implements well. PersonalCEF takes the opposite approach: keep upstream cefclient structure and Chrome-style UI, then add only a thin Windows integration layer.

## Current features

- Chromium/CEF native Chrome-style tabs and browser UI
- Persistent per-app profile/cache
- Optional per-app SOCKS5 proxy
- Optional SOCKS-side DNS resolution and QUIC disable
- Single-instance activation
- Close/minimize to Windows tray
- Configurable home page and language
- Optional HTTP/resource diagnostic logging
- VS2022 + CMake build workflow
- CEF version pinned by `CEF-VERSION.txt`

## Build

Requirements: Windows 10/11 x64, Visual Studio 2022 with C++ desktop tools, CMake and PowerShell.

```bat
START-HERE.cmd
```

The scripts prepare the pinned CEF SDK, synchronize the official cefclient framework, apply the PersonalCEF patches and generate the VS2022 solution.

For a release build:

```bat
BUILD-RELEASE-AND-RUN.cmd
```

## Configuration

Copy `set.default.ini` to `set.ini` and edit as required. The public defaults keep the proxy disabled.

```ini
[proxy]
enabled=1
type=socks5
host=127.0.0.1
port=1080
bypass_list=<local>;localhost;127.0.0.1
disable_quic=1
```

## Design rule

**Let Chromium own browser UI, tabs, processes and navigation whenever possible.**

PersonalCEF should add capabilities around Chromium rather than replace working Chromium behavior with a second browser framework.

## License

PersonalCEF-specific source is released under the MIT License. CEF, Chromium and other third-party components keep their own licenses.
