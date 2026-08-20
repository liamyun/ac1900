# Known-Good Baseline

This document separates **observed/verified behavior** from assumptions and unresolved work. Future AI agents should not promote an item from "under investigation" to "verified" without actual Windows testing or explicit evidence.

## Pinned platform

- CEF: `151.3.16+gbe1e15d+chromium-151.0.7922.109_windows64`
- CEF commit: `be1e15d`
- Target: Windows x64
- Toolchain: Visual Studio 2022 + CMake
- Main runtime style: `CEF_RUNTIME_STYLE_CHROME`

## Verified in the development line

### Native Chromium UI / tabs

The main window uses Chromium's Chrome runtime style rather than a custom tab implementation. Native Chrome-style tabs/windows were observed working and became the preferred architecture.

**Status: VERIFIED**

### Persistent application profile/cache

PersonalCEF uses its own persistent runtime profile/cache rather than an ephemeral browser context.

**Status: VERIFIED**

### Thin-wrapper architecture

The smoothest and most reliable direction observed during development is to keep Chromium responsible for browser UI, tabs, navigation and browser processes, while PersonalCEF adds only application-specific integration.

**Status: VERIFIED AS THE CURRENT DESIGN BASELINE**

### SOCKS command-line configuration

The runtime applies proxy configuration on the Chromium command line before browser initialization. Diagnostic logging has shown the intended proxy switches present in Chromium startup configuration.

**Status: VERIFIED AT CONFIGURATION LEVEL**

Important: seeing the switch is not proof that every possible Chromium protocol or subsystem follows the same route. DNS, QUIC/UDP and application-layer server policy must be diagnosed separately.

### HTTP connectivity through the configured browser network stack

During ChatGPT testing, the browser successfully loaded HTML/static resources and received normal HTTP responses, including both 200 and 403 responses from backend endpoints.

**Status: VERIFIED**

An HTTP 403 must not be described as a basic TCP/proxy-connect failure merely because a proxy is enabled.

## Working behavior that must receive regression testing on future changes

These behaviors were implemented in the current development line and should be explicitly checked after changes:

- native Chrome-style tabs
- single-instance activation/wake
- close/minimize-to-tray policy
- explicit Exit actually exits
- login helper remains a normal foreground/taskbar window and does not inherit main-window tray behavior
- return-to-main behavior from login helper
- persistent profile/cache location
- proxy startup configuration

Do not silently modify these while working on an unrelated feature.

## Under investigation / not proven

### ChatGPT first-load backend 403 behavior

Observed behavior has included successful page/static-resource loading while selected `/backend-api/...` requests returned HTTP 403, with later requests sometimes returning 200 after another navigation/reload.

The diagnostic data does **not** support the simplistic claim that the top-level document having no `Authorization` header means authentication was not initialized. Backend requests were observed with authorization present.

Potential causes must remain evidence-driven (for example application/auth/CDN/WAF/challenge state) until diagnostics identify the exact difference between failing and succeeding requests.

**Status: UNDER INVESTIGATION**

### Full Windows rebuild from the public repository snapshot

The public source snapshot was assembled and statically checked outside the final Windows build environment. It must not be described as a clean-room public-repository build until someone clones it on Windows, runs the documented preparation/build path and verifies the resulting binaries.

**Status: NEEDS CLEAN-CLONE WINDOWS VERIFICATION**

## Definition of verification

Use precise language in future documentation and issues:

- **STATICALLY INSPECTED** — source/configuration reviewed only
- **COMPILED** — compiler/linker completed successfully
- **LAUNCHED** — resulting application started
- **BEHAVIOR VERIFIED** — the specific behavior was exercised and produced the expected result

Never collapse these into a generic "tested" claim.
