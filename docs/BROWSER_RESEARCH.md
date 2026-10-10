# The embedded browser: research and decision (0.7.0)

Question: can AKENO STREAM show web content **inside** the app (Websites mode,
YouTube's official player, crunchyroll.com) instead of handing the user to
the separate PS5 web browser? What does the console really offer, and what is
proven where?

## Answer in short

| Option | Verdict | Evidence |
| --- | --- | --- |
| **`libSceWebBrowserDialog`** (the system browser as a common dialog over the running app) | **Used.** The only WebKit engine a native homebrew title can drive today | Opened from a fake-signed native title on firmware 12.70 by EVO-PLAYER-PS5 (2026-09-26); struct layouts match SharpProspero's binding |
| `libSceWebKit2` embedded directly (own WebView, own render surface) | Not possible with public knowledge | No documented host API for homebrew; WebKit's support library `libScePosixForWebKit` resolves to null in native titles (AKENO's own v0.3 finding); no project has done it |
| A self-contained engine (WPE WebKit, Servo, Ladybird, NetSurf, litehtml) | Not realistic now | See [Alternatives](#alternatives) |
| `sceSystemServiceLaunchWebBrowser` (0.6.0's "Browser" button) | Removed | Starts the separate browser app (not what was asked). It was also looked up with `sceKernelLoadStartModule`, which a fake-signed title cannot use (EVO-PLAYER-PS5's finding), so it most likely never worked |

The existence of the PS5's browser app proves nothing about embedding. What
proves that `libSceWebBrowserDialog` can be embedded is that another native
homebrew title opened it on hardware, with the same layouts AKENO uses.

## What libSceWebBrowserDialog is

It is one of the PS4/PS5 *common dialogs* (like the on-screen keyboard
dialog). A game or app opens it with an address. The system then draws a
WebKit browser over the app's own video output, and the app keeps running and
presenting underneath. The app pumps its status once per frame, and the
browser closes when the user leaves it or when the app closes it. Its user
agent on 12.70 was `Mozilla/5.0 (PlayStation; PlayStation 5/12.70)
AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15`.

What the app gets, and does not get:

- **Gets:** a full WebKit engine (JavaScript, HTML5 media, the system
  on-screen keyboard for text fields, the browser's own cursor and scrolling
  model, TLS with the system's certificate store), rendered by the system.
- **Does not get:** the page's pixels, its cookies, its storage, what the user
  types, navigation events, or a way to run script in the page. The browser is
  a separate system component. For security and privacy this is a feature.
  AKENO cannot leak what it never sees.

### Facts used by `src/platform/ps5/web_view.cpp`

| Item | Value | Source |
| --- | --- | --- |
| Module | `libSceWebBrowserDialog.sprx`, sysmodule id `0x00AB` | SharpProspero `Sysmodule.cs`; psxdev/ps4sdk `sysmodule.h`; EVO-PLAYER-PS5 |
| Order | `sceCommonDialogInitialize` → `sceSysmoduleLoadModule(0xAB)` → `sceWebBrowserDialogInitialize` → `Open` → `UpdateStatus` each frame → `GetResult` → `Close` | SharpProspero `WebBrowser.cs` ("in this order, or that initialize fails"); EVO-PLAYER-PS5 hardware log |
| Common block | 48 bytes: `size`, 36 reserved, `magic` = `0xC0D1A109` + address of the block | SharpProspero `WebBrowserDialog.cs`; psxdev/ps4sdk `commondialog.h` |
| Parameter block | 328 bytes: mode @56 (1 default, 2 custom), user id @60, URL @64, width/height/x/y @80-86, parts @88, header @92-96, control @100 | SharpProspero; EVO-PLAYER-PS5 (same offsets, `_Static_assert`ed) |
| Result | 256 bytes, result code @0 | SharpProspero; EVO-PLAYER-PS5 |
| Status values | 0 none, 1 initialized, 2 running, 3 finished | SharpProspero `CommonDialogStatus` |
| User | `sceUserServiceGetInitialUser`; the system (0xFF) and "everyone" (0xFE) ids are refused | SharpProspero `WebBrowser.cs` |
| Already initialized | `0x80B80002`, not an error | SharpProspero `CommonDialog.cs` |

Hardware results reported by EVO-PLAYER-PS5 on firmware 12.70:

| Question | Result |
| --- | --- |
| Loads and opens from a fake-signed native title | Yes, every call returned 0 |
| Draws over the app's own output | Yes |
| A page reaches a loopback listener in the app (`fetch`, navigation) | Yes, with CORS |
| `sceWebBrowserDialogClose` on a running dialog | Works; finished about 25 frames later |
| Custom rectangle (mode 2) | Opens with parts/control `0/0`, `0x7/0`, `0/0x1`; `0xFF/0xFF` refused (`0x80b8000a`) |
| The library's own callback mechanism | Not usable (never closes on the callback URL) |
| Page storage across openings | **Not kept**: `localStorage` was empty at every new opening |
| Controller while the dialog is up | The system browser's cursor and scroll model. The app should ignore pad input |
| PS button to leave a dialog without browser controls | **Panicked the console**: do not rely on it |
| `sceKernelLoadStartModule` of system modules from a fake-signed title | Fails, so positional imports are needed |

### How AKENO uses it

- **Imports.** The browser and common-dialog modules have no stub in the
  public SDK. `tooling/stubs/*.c` produce link-time stubs (the same mechanism
  as ProsperoTV's `libSceVideodec2`). Only five browser functions and
  `sceCommonDialogInitialize` are imported, all of them called on hardware by
  EVO-PLAYER-PS5. A name the firmware lacks would stop the title from
  loading, so unverified exports (cookies, zoom, navigation) are not imported.
  CI checks that the imports bind to the system modules (`build/imports.txt`).
- **Third-party websites** open as they are, in the default presentation
  (mode 1). The browser's own controls give back/forward, reload, the address
  and a way out. AKENO adds nothing to the page: no proxy, no injected
  script. TLS validation, cookies and same-origin isolation are WebKit's own.
- **AKENO's own pages** (the official YouTube player and the capability test)
  are served from `http://127.0.0.1:<port>/s/<token>/` while they are open
  (`src/web/local_pages.cpp`). This gives YouTube's player the HTTP `Referer`
  it requires (without it the player fails with error 153), and it lets the
  page report what happened. With *Settings → Player pages without browser
  controls* they open full screen (mode 2). They carry a *Back to AKENO*
  button, and AKENO closes the dialog itself if such a page stops answering.
  A page that never loads is closed after 30 seconds.
- **While the browser is open**, AKENO keeps presenting frames and pumping
  the dialog (`BrowserScreen`, `src/app/screen_browser.cpp`) and ignores the
  controller, except L3 + R3 together as an emergency close. That chord works
  only if the system passes those buttons to the app while the dialog is up,
  which is unverified.

## Reference projects

| Project | What it is | Relevance |
| --- | --- | --- |
| [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv) | A web server payload: the PS5's browser (or a PC/phone) is its client | Shows the system browser as a client of a local server, not embedding |
| [blackbearreloaded/ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) | OpenGL UI kit for native titles | No web engine; confirms native titles draw their own UI |
| [blackbearreloaded/ProsperoTV](https://github.com/blackbearreloaded/ProsperoTV) | Native IPTV player (AKENO vendors its decoder backend) | No web engine; HTTP server for a phone remote only |
| [sainsaji/EVO-PLAYER-PS5](https://github.com/sainsaji/EVO-PLAYER-PS5) (`docs/research/web-browser-dialog.md`, `src/evo_webui.c`) | Native media player | **Opened `libSceWebBrowserDialog` from a native title on 12.70**; the hardware results above |
| [SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero) (`Interop/Dialog/WebBrowserDialog.cs`, `Platform/WebBrowser.cs`) | C# SDK for PS5 homebrew | The parameter, result, cookie and allow-list layouts and the call order |

All are GPL-3.0 (or compatible). AKENO re-implements the layouts and copies
no code. The sources are credited in `THIRD_PARTY_NOTICES.md`.

## Not known yet (hardware testing decides)

These are open questions for the console run (see
[HARDWARE_ACCEPTANCE.md](HARDWARE_ACCEPTANCE.md), section 10):

1. Whether the five imports load on firmware **12.20** (EVO-PLAYER-PS5 used
   12.70; the AKENO tester also runs 13.09).
2. Which DualSense buttons the browser uses in the default presentation
   (cursor, scroll, back, close), and whether the app still receives L3/R3.
3. Whether cookies (and so sign-ins) survive closing the dialog and
   restarting the app. The capability test measures cookie and
   `localStorage` persistence.
4. Whether `http://127.0.0.1` counts as a secure context (needed for EME). The
   capability test reports `isSecureContext`.
5. Which formats, MSE and DRM systems the engine offers to pages (capability
   test). WebKit on PS5 is not known to ship a Widevine or PlayReady CDM to web
   content. Until measured, Crunchyroll playback is reported as unknown, not
   as working.
6. Whether YouTube accepts a loopback origin as the embedding site (error 153
   otherwise), and whether the engine's media stack plays YouTube's formats.

## Alternatives

If `libSceWebBrowserDialog` failed on a firmware, these were considered:

- **WPE WebKit or WebKitGTK ported to the PS5.** These are the right engines,
  but a port is months of work. It means JavaScriptCore without JIT (no
  writable-executable memory in a homebrew process), GLib, libsoup, HarfBuzz,
  ICU (about 30 MB of data), a graphics backend on PS5's GPU (only an
  experimental OpenGL layer exists for homebrew), and GStreamer wired to
  `sceVideodec2` for HTML5 video. It would still have no DRM: Widevine and
  PlayReady CDMs are licensed binaries that cannot be obtained, so Crunchyroll
  would not play either. Memory: hundreds of MB.
- **Servo or Ladybird.** Not ready for such a port (Rust and Skia/Qt
  toolchains, multi-process designs), and no media pipeline for the PS5.
- **NetSurf or litehtml.** Small and portable C/C++, but with essentially no
  modern JavaScript and no HTML5 video. They cannot run YouTube, Crunchyroll
  or most sites.

Conclusion: the system's own browser dialog is the only realistic engine. It
needs no extra memory in AKENO's process (the system renders it), uses the
system's certificate store and updates with the firmware.
