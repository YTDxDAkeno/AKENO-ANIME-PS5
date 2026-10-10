# Native playback for publicly declared media (experimental)

This feature lets a user try **DRM-free video in AKENO's native PS5 player**
without relying on the system browser to decode it.

## How to try

- Go to **Websites -> Find Public Video on a Page**, enter a page URL, and select a result.
- Or select a saved site with **Square -> Find public videos (native)**.
- The feature scans ordinary public HTML for explicit `<video src>`, `<source
  src type=video/...>`, HLS source types, and direct `og:video` metadata.
- AKENO shows available direct URLs and starts its existing video engine on selection.

It does **not** execute JavaScript, inspect the system browser, use session
cookies, intercept protected streams, log in as the user, guess hidden media
addresses or bypass DRM. A streaming web page with only dynamic player code
or protected content will usually return **Native video not found**.

It may not work with media requiring a special authorization, a separate
Referer, or with a site that prohibits direct playback. The resulting
URL is played using AKENO's existing HTTPS player, without browser
credentials. This is a focused experiment, **not** a promise that adding
an arbitrary website will make its entire library native.

## Technical limits

- One HTTPS/HTTP GET, max 384 KiB of HTML; no cookie headers.
- Up to 16 explicitly listed media URLs, duplicates removed.
- No authentication, stream extraction, DRM, JavaScript or third-party API.
- If the page is too large, a site needs dynamic JavaScript, or the media
  does not appear in HTML, it will not be found.
- General-purpose playlist/source support remains in **Sources**.
- Avoid using the tool with content you are not authorized to play.

## Hardware test

1. Keep the previous v1.0.0 release candidate as a rollback.
2. Install this development-branch artifact only for testing.
3. Test with an HTML page you own that directly contains
   `<video src="/clip.mp4" controls>`.
4. Verify actual picture, audio, stop and replay on firmware 12.20.
5. Confirm browser-based YouTube and native PeerTube still work.

Crunchyroll protected episodes remain unsupported without a licensed
CDM/EME. A separate browser test on this user's PS5 reported no EME and
an MSE fragmented-MP4 playback failure; this feature does not change
either capability.
