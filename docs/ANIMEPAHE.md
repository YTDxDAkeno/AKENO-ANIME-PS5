# AnimePahe: investigation and integration report

AnimePahe (`animepahe.pw`; also `.com` and `.org`, earlier `.ru` and `.si`)
plays in AKENO STREAM's browser on a PS5 with firmware 12.20, and - unlike
Miruro - its episodes start without turning on an Autoplay setting. What that
proves, what AnimePahe is built on, and whether AKENO STREAM may play its
episodes in its own player: investigated 2026-10-11 for AKENO STREAM
1.1.0-rc.2.

**Result in short**

| Question | Answer |
| --- | --- |
| AnimePahe's episodes in AKENO STREAM's own player | **No.** Its catalogue and episode lists sit behind DDoS-Guard's browser challenge; each episode is a `kwik.cx` web player whose HLS stream is produced by an obfuscated script at run time and served only to requests carrying kwik's `Referer`. Every route to a stream file needs challenge solving, session-cookie extraction, script de-obfuscation and a forged `Referer`. No documented API, terms or licence permit third-party playback. Nothing was built, nothing is claimed. |
| AnimePahe's catalogue in AKENO STREAM | **No.** Unlike Miruro, AnimePahe's pages cannot be read the way a browser reads them without passing the DDoS-Guard JavaScript challenge, which AKENO STREAM's HTTP client does not run and never solves. AKENO STREAM sends AnimePahe no request at all. |
| What AKENO STREAM does with an AnimePahe address | *Add Streaming Provider* recognises it **without contacting it**, shows the reasons and the label **Browser only (PS5 browser)**, adds nothing, and offers **Open in Websites**. There, AnimePahe's own player plays it - not AKENO STREAM's. |
| Why it plays in the PS5 browser | The PS5 browser is a complete WebKit browser: it runs the challenge, keeps the cookies, runs AnimePahe's and kwik's scripts, and requests the stream from inside kwik's own page (so with kwik's `Referer`). Decoding works through MediaSource. Details below. |

## 1. Confirmed on the console

Reported by the tester on a PS5, firmware 12.20, AnimePahe opened in AKENO
STREAM's embedded browser (Websites):

| Test | Result |
| --- | --- |
| `https://animepahe.pw/anime` loads | Yes |
| Titles listed in an alphabetical index | Yes |
| Episode lists visible | Yes |
| An episode can be chosen | Yes |
| The chosen video starts | Yes, **without** an Autoplay setting (Miruro needs Autoplay on) |

Not reported yet: sound, seeking, pause/resume, several series. The hardware
checklist (section 15) asks for them.

## 2. What this proves - and what it does not

Deductions from the console results, not separate measurements:

- **The PS5 browser can decode and play AnimePahe's streams.** WebKit's
  MediaSource path decodes the video and audio AnimePahe serves (HLS through
  hls.js, see section 3). That is the same browser pipeline that plays
  Miruro's video once started and YouTube's embedded player.
- **A press on AnimePahe's player starts playback.** With Miruro, a press
  without Autoplay does not (docs/MIRURO.md, tests A/B). A likely difference,
  **not measured**: AnimePahe's Play control is kwik's own Plyr button inside
  the kwik frame, and kwik's script calls `play()` directly in that press,
  while Miruro's player fetches its stream through a private interface after
  the press (Miruro hypothesis H2). If so, a press inside another origin's
  frame does count on 12.20, which argues against Miruro hypothesis H3. The
  Playback Lab's *Test a Play button* (`start.press_frame`,
  `start.press_after_load`) measures exactly these two cases on the console.
- **It does not prove that AKENO STREAM's native player can load the same
  resource.** The native player is a separate HTTP client (libcurl) with no
  JavaScript engine, no browser cookies and no page context. The stream
  address does not exist in any document AKENO STREAM could read: kwik's
  obfuscated script builds it at run time inside the browser, and the stream
  host answers only requests that carry kwik's page as `Referer`. Reproducing
  that outside the browser is exactly what section 5 rules out.
- **Codec compatibility is likely but not measured, and is not the question.**
  AKENO STREAM's player plays HLS with H.264 and AAC (console-verified with
  PeerTube on 12.20). AnimePahe's codec was not measured: its servers were not
  contacted (section 6). Even a perfectly compatible codec gives no permission
  to fetch the stream.

## 3. How AnimePahe is built

Read 2026-10-11 from three unofficial client projects (cloned and read, never
run). `animepahe.pw`, `animepahe.com`, `animepahe.ru` and `everythingmoe.com`
could not be reached from the build environment (its network policy blocks
them), so no AnimePahe page, terms or `robots.txt` was read directly. Nothing
below was tried against AnimePahe's or kwik's servers.

| Project | Commit | What it is |
| --- | --- | --- |
| [ElijahCodes12345/animepahe-api](https://github.com/ElijahCodes12345/animepahe-api) | `5b152c1` (2026-07-05) | "An unofficial REST API for Animepahe" (`animepahe.pw`, iframe host `kwik.cx`) |
| [Pal-droid/Animepahe-API](https://github.com/Pal-droid/Animepahe-API) | `02e039c` (2026-05-10) | "An actually working AnimePahe API that can bypass Cloudflare" (`animepahe.si`/`.com`) |
| [Shineii86/AnimePaheAPI](https://github.com/Shineii86/AnimePaheAPI) | `ee15ba2` (2026-07-25) | Scrapes `animepahe.ch` - a **different, slug-based site** (`/one-piece-episode-1170-english-subbed/`, MP4 hosts `turbovidhls`/`etvp`), not AnimePahe's structure. Used only for its description of the protection. |

| Part | How it works |
| --- | --- |
| Alphabetical index | `GET /anime`: one HTML page with tab panes `#hash` and `#A` ... `#Z`; each entry is a link with the title and a type badge (TV, Movie, OVA, ONA, Special, Music). Genre and theme lists: `/anime/<tag>/<value>`. |
| Search | `GET /api?m=search&q=<text>&page=<n>`: JSON with page fields (`total`, `per_page`, `current_page`, `last_page`) and `data[]`. |
| Airing list | `GET /api?m=airing&page=<n>`: the same JSON shape. |
| Anime details | `GET /anime/<anime session id>`: HTML (poster, synopsis, information fields). `<meta>` tags name the series' ids in public databases: AniDB, AniList, Anime-Planet, ANN, Kitsu, MyAnimeList. |
| Episode metadata | `GET /api?m=release&id=<anime session>&sort=episode_asc\|episode_desc&page=<n>`: JSON pages; each episode has `episode`, `episode2`, `edition`, `title`, `snapshot` (image), `audio`, `duration`, `session`, `filler`, `created_at`. |
| Pagination | The `/api` answers are paged (`current_page`, `last_page`); the A-Z index is one page. |
| Episode page | `GET /play/<anime session>/<episode session>`: buttons in `#resolutionMenu` with `data-src` (a `kwik.cx/e/<id>` player page), `data-resolution`, `data-audio` (`jpn`/`eng`) and `data-fansub`; download links in `#pickDownload` (`pahe.win/<id>`, leading to a kwik `/f/` page with a token form and an MP4 file). |
| Player | `kwik.cx/e/<id>`: a page whose packed script (`eval(function(p,a,c,k,e,d)...)`) sets up **Plyr** with **hls.js** and an `.m3u8` address on another host. The clients extract that address by running the script in a sandbox (Node `vm`, JSDOM) or by regular expressions. |
| Media access | The stream host and the MP4 downloads answer only with kwik's page as `Referer` (animepahe-api: "you MUST pass the provided `downloadPage` URL as the `Referer` header"). |
| Protection | **DDoS-Guard** in front of AnimePahe (a JavaScript challenge, `#ddg-cookie`, `__ddg*` cookies; also listed by Wappalyzer). The clients get past it with a headless Chromium (Playwright) that hides `navigator.webdriver` and other automation signs, then reuse its cookies for 14 days; others spoof TLS fingerprints (`got-scraping`) or use `cloudscraper`. Data-centre addresses are blocked ("DDoS-Guard blocks requests from datacenter IPs"). |
| Domains | `.pw` now; `.com` and `.org` listed as mirrors (EverythingMoe); `.ru` and `.si` used earlier. A look-alike at `.ch` is another site. |

There is no official AnimePahe API: AnimePahe documents none, `/api` is the
site's own internal endpoint, and every project above is unofficial and
reverse-engineered.

## 4. Third-party integration permissions

| Criterion | Finding |
| --- | --- |
| Documented API for third parties | None found. `/api?m=...` is the site's internal endpoint. |
| Terms permitting third-party clients or playback | None found (the site could not be read from the build environment; no project cites any). |
| Licence for the series | None published. EverythingMoe lists AnimePahe (rank 2 of 86 active anime-streaming entries, June 2026) **without** its `licensed` tag, which marks official services; its notes: "Self-host", "Hard-sub", "Easy download", "Add watermark". |
| Redistribution or embedding permission | None found. |
| DRM | None observed in the projects (plain HLS and MP4) - which does not make the video authorized. |

## 5. Native playback: the actual blockers

AKENO STREAM's player could very probably play the format. Format is not the
blocker. These are, each one a line AKENO STREAM does not cross:

| Category | Blocker |
| --- | --- |
| Access control | Catalogue, episode lists and episode pages are behind DDoS-Guard's JavaScript challenge. Reading them means solving the challenge (a headless browser) or impersonating a browser's TLS fingerprint. **Challenge solving: not done.** |
| Protected session | The clients reuse the cookies a solved challenge left behind. **Protected-session extraction: not done** - AKENO STREAM also never reads the PS5 browser's cookies. |
| Obfuscated, undocumented stream endpoint | The stream address exists only inside kwik's packed script. Extracting it means running or de-obfuscating that script. **Using undocumented streaming endpoints: not done.** |
| Hotlink protection | The stream and MP4 hosts answer only kwik's `Referer`. **Forging it: not done.** |
| No authorization | No API, terms or licence; not a licensed service. |

So no native AnimePahe playback was built, and none is claimed. No AnimePahe
episode was played in AKENO STREAM's player, on the build machine or on the
console. A generic open-licence film playing natively proves AKENO STREAM's
player, not AnimePahe.

## 6. What AKENO STREAM does now

| Where | Behaviour |
| --- | --- |
| *Add Streaming Provider* with an AnimePahe address (`animepahe.pw`, `.com`, `.org`, `.ru`, `.si` and their subdomains) | Recognised **without a request**: "AnimePahe cannot be a native provider: its catalogue and episodes sit behind a DDoS-Guard browser check, and its videos are kwik web players whose streams answer only that player. Nothing was sent to the site." The four reasons and this document are listed; the label is **Browser only (PS5 browser)**. Nothing is added. |
| The button next to *Detect Provider* | **Open in Websites**: opens the address in the PS5 browser directly (no "check the site first" request from AKENO STREAM), with "AnimePahe plays in the PS5 browser on firmware 12.20: its own player, not AKENO STREAM's." |
| *Websites* | Unchanged: AnimePahe opens and plays as the tester reported. That is browser playback and is never labelled native. |
| A provider entry for AnimePahe written into a provider file by hand | Kept, never contacted, explained (as for Anikoto). |
| Look-alike domains (`animepahe.ch`, `notanimepahe.pw`, `animepahe.pw.example`) and `kwik.cx` | Not treated as AnimePahe. |

Code: `src/anime/site_policy.cpp` (`unsupported_site()`), `src/anime/registry.cpp`
(`check_source_once()`), `src/app/screen_anime.cpp` (`AddProviderScreen`).
Tests: `SitePolicy.AnimePaheIsBrowserOnlyAndNeverContacted`,
`ProviderEndToEnd.AnimePaheIsBrowserOnlyNotAddedAndNeverContacted` (through
the controller; asserts that not one request goes to AnimePahe or kwik).

## 7. What would change the answer

- AnimePahe, or the rights holders, publishing a documented API and DRM-free
  video files licensed for third-party players. AKENO STREAM's website
  provider interface ([WEBSITE_PROVIDERS.md](WEBSITE_PROVIDERS.md)) and native
  player would cover the rest, and a console run on firmware 12.20 would be
  the acceptance test.
- For watching AnimePahe today: Websites, as confirmed on the console.
