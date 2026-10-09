# Akeno Anime PS5 v0.2 – Test auf Firmware 12.20

Dies ist eine **native PS5-Technikvorschau**. Sie hat keine Crunchyroll-Streaming-Integration.

## Was v0.2 neu kann
- Zertifikatsgeprüfter HTTPS-GET zu https://example.com/ direkt von der PS5 (libcurl + OpenSSL, mit der Konsolen-CA).
- Ein zweiter HTTPS-GET ruft das freie HLS-Mastermanifest https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8 ab.
- Die App prüft, ob der Download mit HTTP 200 erfolgreich war, ob die Datei mit `#EXTM3U` beginnt und zählt `#EXT-X-STREAM-INF`-Einträge.
- Diese Prüfungen passieren in einem Worker-Thread und blockieren nicht die Controller-Oberfläche.
- Netzwerkstatus und Fehlercodes stehen live unter **DIAGNOSE**.

## Das kann v0.2 weiterhin NICHT
- Keine eigentliche MP4/HLS-Videodekodierung, kein Bild/Ton einer Episode.
- Keine Crunchyroll-Kontoanmeldung, keine Crunchyroll-API, kein DRM.
- Ein grünes `HLS PLAYLIST` bedeutet nur, dass das Manifest lesbar ist, **nicht**, dass ein Anime gespielt werden kann.

## Installieren ohne WSL
1. GitHub-Repositorium > Actions > **Akeno Anime PS5 Preview** öffnen. Aktuellen Lauf abwarten.
2. Unter Artifacts `AKENO-ANIME-PS5-PPSA99276` herunterladen (GitHub-Login erforderlich).
3. Die äußere Actions-ZIP und dann `PPSA99276.zip` entpacken.
4. Vollständigen Ordner `PPSA99276` per FTP nach `/data/homebrew/PPSA99276` übertragen.
5. App vollständig schließen, ShadowMountPlus neu starten und Akeno Anime öffnen.

## Netzwerk testen
- **R1** viermal drücken, bis `DIAGNOSE` ausgewählt ist.
- **X** drücken; die App zeigt `TEST LAEUFT`.
- Auf `OK HTTP 200` bei HTTPS und HLS achten.
- Falls ein Fehler erscheint, Screenshot mit *HTTP- und CURL-Fehlercode* senden. Die genauen Werte sind für die Firmware 12.20 noch nicht auf Hardware überprüft.

## Build
Native Build basiert auf [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
Benötigt `PACBREW_PACKAGES += libcurl`, `APP_WRAP_SYMBOLS += fcntl` und den dazugehörigen `console_curl.c`-Adapter aus der Boilerplate. Die GitHub-Actions-Datei installiert die öffentlichen Build-Werkzeuge automatisch.

## Sicherheit
- HTTPS-Peer- und Hostnameprüfung bleiben aktiviert.
- Maximal 128 KiB je Antwort, Timeout 16 Sekunden, HTTPS-only auch für Redirects.
- Keine Speicherung persönlicher Zugangsdaten.
- Keine DRM-Umgehung.
