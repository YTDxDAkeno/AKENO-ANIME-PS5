# Crunchyroll integration gate — NOT IMPLEMENTED

**Objective**: Play content a user is entitled to watch on a PS5 with firmware 12.xx, without PSN and without modifying their system firmware.

**Current technical conclusion (2026-10-09)**: No proven, authorized route to Crunchyroll account login and DRM-protected playback from a native PS5 homebrew app was identified. A UI and HTTPS stack are insufficient.

## Required before claiming success

1. Document a legitimately authorized Crunchyroll authentication flow for a third-party PS5 client. Do NOT request the user's password in a homemade login screen or imitate device identities.
2. Document an authorized catalog API, rate limits, user entitlements, and allowed artwork usage. Do NOT ship private credentials or scrape catalog data.
3. Document a legitimate, platform-compatible DRM license and CDM integration. Do NOT bypass DRM, retrieve raw encryption keys or replay device certificates.
4. Verify hardware decode / audio output / timed subtitles / variable bitrate switching on a jailbroken firmware 12.xx console.
5. Run end-to-end testing: account login, entitled episode starts, audio and video both play, seek/pause/restart works, token expiry, network failure and logout.

Until gates 1-3 are resolved, the app must keep `authentication` and `protected_video` as unavailable; it must not display a simulated success.

## Current UI and platform stage

- Standalone title identity PPSA99276.
- Real-time drawn technical preview and DualSense L1/R1 navigation, D-pad select, Cross modal, Circle back.
- No PSN requirement for the *preview* itself; this does not prove Crunchyroll access.
- System imports for controller need on-console verification with the chosen loader and firmware.

## Security

Do not collect Crunchyroll credentials or tokens until an approved provider integration has been defined. Do not include user identifiers in hardware logs. HTTPS must always validate host certificates.
