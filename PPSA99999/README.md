# Akeno Anime — PS5 Native Homebrew Technical Preview v0.1

**Important:** This is actual C++ source for a native PS5 *technical preview*, NOT a functioning Crunchyroll player. It has not been built for PS5 or run on PS5 hardware in this session. It does not sign in to Crunchyroll, access the Crunchyroll catalog or play Crunchyroll videos. Please do not install it expecting video playback.

## Basis

Uses the third-party open-source [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) and its `src/demo_renderer.hpp`/`.cpp` VideoOut layer, which were tested by upstream on 12.70 (not every 12.xx version). The boilerplate is GPL-3.0-or-later. Source-derived controller ABI comments also credit ps5-homebrew-ui.

## Setup on Windows 11 using WSL Ubuntu

1. Install WSL Ubuntu and the prerequisites listed in the upstream boilerplate README.
2. Create a new folder from the upstream boilerplate: `git clone https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git akeno-anime-app`.
3. Copy **everything in this ZIP** into that checkout, overwriting the template's `src/main.cpp` and `sce_sys/param.json` and icon. Do not delete the template's `Makefile`, `src/demo_renderer.*`, SDK setup files, `runtime`, or `tools`.
4. From inside the checkout, run `make doctor` and then `make`. It generates `dist/PPSA99276/` and `dist/PPSA99276.zip` if the PS5 compiler/toolchain and dependencies work.
5. Copy the built *title folder* `PPSA99276/` to `/data/homebrew/` on your jailbreak PS5 with ShadowMountPlus/compatible loader; restart the loader. The ZIP in this source package is NOT a PS5-installable app.

To run only the model tests (host Ubuntu or Linux):

```sh
c++ -std=c++20 -Wall -Wextra -Werror -pedantic -Isrc src/app_model.cpp tests/app_model_test.cpp -o /tmp/akeno-app-model-test
/tmp/akeno-app-model-test
```

## Controls in this stage

- L1/R1: Switch tabs.
- D-pad Up/Down: Select an informational row.
- Cross: Show the integration limitation dialog.
- Circle: Close dialog or return to Start.

**Known limits:** The simple CPU renderer updates the full 1080p image every frame and is not the final accelerated app UI. The controller ABI and import resolution still need PS5 verification. No network catalog, user login, DRM integration, playable video or subtitles. See `docs/PLAYBACK_FEASIBILITY.md`.

## Development direction

Once a legitimate playback integration is available, migrate UI to the [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) GPU kit, put HTTPS on a separate worker thread, and build the video/audio renderer around actual licensed content access. Do not claim the app can play Crunchyroll streams until end-to-end hardware playback is demonstrated.
