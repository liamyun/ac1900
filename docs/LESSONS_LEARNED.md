# Lessons Learned / Dead Ends

This document records mistakes, failed approaches and diagnostic conclusions from the PersonalCEF development process. Its purpose is to save future maintainers — especially AI agents — from paying the same cost again.

## 1. Rebuilding tabs around cefclient was the wrong direction

### Attempt
Use the ordinary cefclient `RootWindow` / `ViewsWindow` framework and build or emulate a multi-tab browser around it.

### Result
The result was more fragile and did not feel like Chromium's own tabbed browser. Window/process interactions became harder to reason about, and attempts to manually manage/reparent browser surfaces increased the risk of freezes and regressions.

### Lesson
If the goal is Chrome-like tabs, let **Chromium create the Chrome-style window and tabs**. Do not rebuild a second tab/window system unless there is a compelling requirement that native Chrome runtime style cannot satisfy.

---

## 2. A popup accidentally demonstrated the correct architecture

A useful turning point occurred when a CEF-created popup displayed native Chrome-style tabs while the original main window did not. That proved the native functionality already existed in the selected CEF build; the issue was the main-window creation path, not a missing tab implementation.

### Lesson
When Chromium already demonstrates the desired behavior in another window, first compare creation paths and runtime style before writing new UI code.

---

## 3. Mixing Chrome-style windows with old RootWindow assumptions causes confusion

After moving the main browser to Chrome runtime style, code that assumed the main UI still belonged to cefclient `RootWindow` / `ViewsWindow` became invalid.

One symptom was an attempted permanent status bar that was conceptually tied to the old UI ownership model. Desktop Chrome itself does not provide a permanent bottom status bar; it has transient status bubbles for things such as hovered links.

### Lesson
Once the main window is Chromium-owned, do not assume old cefclient Views containers still own the visible window hierarchy. Verify ownership before attaching UI.

---

## 4. New features must not regress unrelated working behavior

A major regression occurred when a new full package was based on an earlier baseline and accidentally omitted a later login-helper fix. Previously solved behaviors returned: login window hiding/tray behavior changed and close/return-to-main behavior regressed.

### Lesson
Do not produce a new "full" version from an older baseline without reconciling every later patch. Maintain a known-good feature checklist and make unrelated changes as overlays when possible.

Useful regression checklist:

- main Chrome-style tabs still work
- login helper remains foreground/normal-taskbar behavior as intended
- login helper does not inherit main-window hidden/tray semantics
- close/minimize-to-tray still works
- true Exit still exits
- single-instance wake still restores the existing window
- profile/cache path is unchanged unless deliberately migrated
- proxy behavior is unchanged during UI-only work

---

## 5. Close-to-tray on Chrome-style windows may need more than one interception point

Intercepting only one Win32 close message was not always enough because Chromium can route close behavior through its own Chrome command layer.

The working direction combined multiple layers, including Win32 close/minimize handling and Chrome command interception for the close-window command.

### Lesson
For Chromium-owned top-level windows, distinguish:

- mouse/titlebar non-client events
- `WM_SYSCOMMAND / SC_CLOSE`
- `WM_CLOSE`
- minimize paths
- Chromium's own close command
- intentional program exit

Also maintain an explicit "true exit requested" flag so tray hiding does not swallow a deliberate Exit action.

---

## 6. Apply proxy settings deterministically before browser initialization

Earlier proxy code appended switches. Because command-line configuration can be touched in more than one legitimate CEF phase, blindly appending may leave duplicates or conflicts.

### Better approach
Before applying the desired proxy settings:

- remove conflicting proxy switches
- replace rather than blindly append
- set the final proxy configuration before `CefInitialize`

### Lesson
When debugging startup networking, first log the **effective command line**, not the configuration file you hoped was applied.

---

## 7. SOCKS5 and DNS need to be treated separately

`--proxy-server=socks5://host:port` routes Chromium TCP URL traffic through SOCKS5, but DNS leakage/lookup behavior may still require explicit resolver rules depending on the desired threat model and browser behavior.

`host-resolver-rules` can be used to force unresolved names away from direct local resolution while excluding the SOCKS proxy host and loopback addresses.

QUIC is UDP and does not use the same SOCKS5 TCP path, so disabling QUIC is sensible for a deterministic SOCKS-only network path.

### Lesson
"Using SOCKS" and "all name resolution/network protocols follow SOCKS" are not identical statements.

---

## 8. HTTP 403 is not evidence that SOCKS failed

During ChatGPT testing:

- static HTML/JS/CSS resources returned HTTP 200
- many authenticated `/backend-api/...` requests returned HTTP 403
- later navigation/reload could make the same classes of requests return 200

This means the request successfully traversed the network far enough to receive an HTTP response. The failure was higher-level than "proxy cannot connect".

### Lesson
Classify failures correctly:

- DNS/connect/TLS failure -> transport/proxy investigation
- CEF URL request error -> transport/browser networking investigation
- HTTP 4xx/5xx -> application/auth/security/CDN investigation

Do not randomly rewrite proxy architecture in response to an application-layer 403.

---

## 9. Instrumentation beats timing guesses

An early interpretation suggested that a 403 condition "recovered after several seconds." The user correctly pointed out that a manual refresh may have occurred during that interval.

The next diagnostic version added navigation transition logging so a reload could be identified directly.

### Lesson
Never infer cause from elapsed time when the application can log the causal event. Record reload/navigation transitions explicitly.

---

## 10. Diagnostic scripts themselves must be tested

A filtering batch file generated an invalid PowerShell path (`$p=' '`) and therefore failed even though the actual browser diagnostic log existed.

### Lesson
Treat helper scripts as production code. A broken log-filter script can waste as much time as a broken browser hook. Prefer simple path discovery and print the selected source path before filtering.

---

## 11. Do not log secrets just because diagnostics are enabled

Network debugging needed to determine whether Cookie/Authorization state existed, but recording their values would create unnecessary risk.

### Lesson
Log metadata such as:

- cookie present: yes/no
- cookie length
- authorization present: yes/no
- response status
- selected non-secret headers

Avoid raw Cookie, Authorization, tokens and sensitive bodies.

---

## 12. Public source must be de-personalized

A local working configuration may contain private LAN addresses, paths, cache state and logs that should not become repository defaults.

### Lesson
Public defaults should:

- keep proxy disabled by default
- use example addresses such as `127.0.0.1:1080`
- ignore `set.ini`
- ignore runtime cache/profile/logs
- avoid user-specific absolute paths
- use a project-owned icon rather than a misleading third-party brand imitation

---

## 13. Static validation is not a Windows build

Some project iterations were prepared in a non-Windows tool environment. Source/API consistency could be checked, but Visual Studio/CEF linkage could not be truthfully declared successful until the user compiled and ran it on Windows.

### Lesson
Always state one of:

- statically inspected
- compiled
- launched
- behavior verified

Do not collapse those into "tested."

---

## 14. The highest-value design decision: keep the wrapper thin

The project became noticeably smoother when custom browser-core behavior was removed and native Chromium behavior was allowed to do the heavy lifting.

### Final lesson
The default answer to a new feature request should be:

> **Can Chromium/CEF already do this natively? If yes, expose or configure that. If not, add the smallest possible layer around it.**

This principle has saved more complexity than any individual patch.
