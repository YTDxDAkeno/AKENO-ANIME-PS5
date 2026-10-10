# Crunchyroll in AKENO STREAM

Status for 1.0.0: **the website and sign-in work in Websites; protected
episodes do not play, and a native Crunchyroll mode is not available.** This
page records what was measured, what an authorized integration would need,
and why AKENO STREAM does not work around it.

## What was observed on the console (firmware 12.20)

| Step | Result |
| --- | --- |
| crunchyroll.com in AKENO's browser (Websites) | Loads |
| Sign-in on Crunchyroll's own page | Works |
| Episode pages | Load |
| Episode playback | Stops with **KAT-6005** |
| Playback Lab / browser test, DRM key systems | **No usable key system** (Widevine, PlayReady, FairPlay unavailable to web pages) |

KAT-6005 is the code Crunchyroll's web player shows when the browser offers
no DRM key system it accepts; the same code is reported on desktop browsers
whose Widevine module is not registered
([Opera forum report](https://forums.opera.com/post/407277)).
Crunchyroll's help centre treats DRM errors as a browser capability problem
("enable protected content", update the browser)
([Crunchyroll: DRM Error](https://help.crunchyroll.com/hc/en-us/articles/20757542357396-What-do-I-do-with-a-DRM-Error)).
The PS5's system browser dialog does not expose a licensed DRM module to web
pages, and a homebrew app cannot add one.

## What a native integration would need

| Requirement | Status |
| --- | --- |
| An authorized account connection (OAuth/device sign-in and client credentials issued to the app) | **Not available.** Crunchyroll publishes no public API or developer programme; existing libraries are unofficial reverse-engineered clients that Crunchyroll changes without notice (for example [crunchyroll-rs](https://github.com/crunchy-labs/crunchyroll-rs)). Using them would mean impersonating Crunchyroll's own apps. |
| Episode metadata, seasons, audio and subtitle languages | Partly available without an account: AniList lists Crunchyroll's titles and episode pages; Anime shows them as *catalogue only* entries with an official QR link. |
| Playback of protected streams | **Not available.** Episodes are delivered with commercial DRM. Decrypting them needs a licensed content decryption module (Widevine/PlayReady) that the licensor approves for the device and app. On the PS5 that capability belongs to Sony-approved apps; homebrew has none. |
| Licensed CDM in AKENO's native player | **Not possible** for a homebrew title. Extracted device keys or unlicensed CDMs are circumvention and are not used. |

A browser sign-in does not authorize a different application to fetch or
decrypt the account's streams, and AKENO never copies browser cookies,
sessions or tokens into its player.

## What AKENO STREAM does instead

- Crunchyroll stays usable as a **website** in Websites (browsing, sign-in
  and the account's pages); the official Crunchyroll PS5 app remains the way
  to watch its protected episodes on the console.
- *Anime -> Crunchyroll (native integration status)* and *Websites ->
  Crunchyroll -> Native integration* show this status in the app.
- The Playback Lab (Websites) measures the browser's DRM support on each
  firmware, so a future firmware that exposes a key system is detected.
- The provider architecture ([ANIME_PROVIDERS.md](ANIME_PROVIDERS.md)) keeps
  DRM, sign-in and region as explicit availability states. An authorized
  integration (a partner API with documented sign-in, or DRM-free licensed
  streams) can be added as a provider type without changing the player or
  the Anime screens.

## Not done, on purpose

No credential collection, no cookie or session extraction, no DRM key
extraction, no signature or licence-server bypass, no access to protected
streams outside Crunchyroll's own player. The 1.0.0 release is not blocked on
Crunchyroll: protected playback depends on platform capabilities a homebrew
app does not have.
