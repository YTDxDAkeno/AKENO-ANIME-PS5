# AKENO STREAM 0.6.0 – Installation (Kurzfassung auf Deutsch)

Ausführliche Dokumentation (Englisch): [README.md](README.md).

**Stand:** 0.4.1 läuft auf einer PS5 mit Firmware 13.09 und ShadowMountPlus:
Oberfläche, Controller, HTTPS, Hardware-Video (H.264) mit Ton, ein HLS-Stream,
der Anime-Katalog und der Diagnose-Export funktionierten auf der Konsole. Was
noch nicht auf der Konsole ausprobiert wurde, steht in der README. 0.5.0
(Sources, fMP4-HLS, getrennte Tonspuren, Web-Dateien) lief laut Tester auf
der Konsole. Neu in 0.6.0 (bisher nur automatisch auf dem PC getestet): der
Modus **Discover** (PeerTube und gemeinfreie Filme aus dem Internet Archive,
direkt in der App abspielbar), **Quellen per Handy** hinzufügen und im
YouTube-Modus **YouTube-App starten** bzw. **im Browser öffnen**
(experimentell). Bitte die [Hardware-Checkliste](docs/HARDWARE_ACCEPTANCE.md)
weiter durchgehen.

## Herunterladen

1. Auf GitHub **Actions** -> Workflow **Build** öffnen.
2. Den neuesten grünen Lauf für den Branch bzw. Pull Request auswählen.
3. Unter **Artifacts** das Paket ohne `-screenshots` herunterladen (z. B.
   `AKENO-STREAM-…` oder `AKENO-ANIME-PS5-PR…`).
4. Die GitHub-ZIP entpacken, darin `PPSA99276.zip` ebenfalls entpacken.

## Auf die PS5 kopieren

1. Den kompletten Ordner `PPSA99276/` per FTP nach
   `/data/homebrew/PPSA99276/` kopieren (ältere Dateien überschreiben).
   **Nicht** die ZIP-Datei hochladen.
2. Einen alten Ordner `/data/homebrew/PPSA99999/` (Hello-World-Test von v0.1)
   löschen, falls noch vorhanden.
3. ShadowMountPlus neu starten bzw. die Homebrew-Liste aktualisieren und
   **AKENO STREAM** starten (getestet mit Firmware 13.09, kein PSN nötig).

Der Ordner enthält `eboot.bin`, `sce_sys/`, `sce_module/libc.prx` und
`assets/` (Schriften und Offline-Testvideos).

## Erster Test

Home -> *Offline Test Clips* -> **A/V Sync Test Clip**: Testbild mit
Sekundenzähler, Dauerton und einem Piepton pro Sekunde. Läuft ohne Internet.
Sind Bild und Ton synchron, funktionieren Hardware-Videodecoder und
Tonausgabe.

## Steuerung

- **L1/R1**: Modus wechseln (Home, Anime, YouTube, Discover, Library, Sources,
  Settings);
  im Player ±60 s spulen
- **Steuerkreuz/linker Stick**: navigieren; im Player links/rechts ±10 s,
  hoch/runter Lautstärke
- **Kreuz**: auswählen, im Player Pause/Weiter
- **Kreis**: zurück, im Player stoppen
- **Dreieck**: Suche (Anime, YouTube, Discover, innerhalb einer Quelle)
- **Quadrat**: Favorit (in Sources: Quelle entfernen); im Player maximale
  Qualität ändern
- **OPTIONS**: Dienst-Infos, im Player Stream-Informationen

## Wo Dateien hingehören

Per FTP **schreiben** kann man nur in den Installationsordner
`/data/homebrew/PPSA99276/` (die App sieht ihn als `/app0`). Den eigenen
Datenordner der App kann man vom PC nur **lesen**, und nur solange AKENO STREAM
läuft: `/mnt/sandbox/PPSA99276_000/download0/akeno/`.

| Was | Wohin (vom PC aus) |
| --- | --- |
| Eigene Videos (MP4, MKV, MOV, TS) | `/data/homebrew/PPSA99276/media/` |
| Eigene Quellen | `/data/homebrew/PPSA99276/sources.txt` (oder `sources.json`) |
| Eigene Stream-Liste (älteres Format) | `/data/homebrew/PPSA99276/streams.json` |
| YouTube-API-Schlüssel (optional) | `/data/homebrew/PPSA99276/youtube-key.txt` |
| Diagnoseberichte abholen | `/mnt/sandbox/PPSA99276_000/download0/akeno/` (App muss laufen) |

Videos erscheinen unter *Library* -> *Media in the install folder*. Nur
Streams und Dateien verwenden, die man ansehen darf. Die YouTube-Datei wird
beim nächsten Start (oder mit Quadrat im YouTube-Modus) übernommen; danach
wieder löschen.

## Eigene Quellen (Sources)

Die App bringt **keine** Quellen mit und sucht auch keine. Im Modus
**Sources** fügt man selbst hinzu, was man ansehen darf – wer was hinzufügt,
entscheidet und verantwortet selbst.

- **Per Handy (am einfachsten):** Sources -> *Add from Phone* zeigt einen
  QR-Code. Mit dem Handy im selben WLAN scannen, Adresse einfügen, *Add to the
  PS5* tippen. Die Seite funktioniert nur, solange der Bildschirm offen ist,
  und nur mit dem angezeigten Code.
- **An der Konsole:** Sources -> *Add a Source* -> Adresse eintippen (R1 öffnet
  die Sonderzeichen `:/?=&`) -> Namen vergeben. *Play an Address* spielt einen
  Link einmalig ab.
- **Vom PC (einfacher):** `sources.txt` nach `/data/homebrew/PPSA99276/`
  kopieren, eine Quelle pro Zeile im Format `Name = https://…` (Zeilen mit `#`
  sind Kommentare). Als Vorlage liegt `sources-example.txt` im App-Ordner; ein
  Update überschreibt die eigene `sources.txt` nicht.

Unterstützt: M3U/M3U8-Listen (Gruppen über `group-title`, Logos über
`tvg-logo`), AKENO-JSON-Feeds, HLS-Playlists, MPEG-TS-Streams und MP4/MKV-
Dateien. DRM-geschützte Einträge, MPEG-DASH (`.mpd`) und Protokolle wie rtmp
oder udp werden mit Hinweis übersprungen. Webseiten werden **nicht** nach
Videos durchsucht, und kein Schutz wird umgangen. Details:
[docs/SOURCES.md](docs/SOURCES.md).

## Discover (PeerTube und Internet Archive)

Im Modus **Discover** laufen Videos direkt in der App:

- **PeerTube** (offene, dezentrale Video-Plattform): neue Videos, Filme,
  Kunst & Animation, Wissenschaft, Kinder sowie Kanäle von Blender Studio
  (offene Filme), Framatube und TILvids. Heikle Inhalte sind ausgeblendet.
- **Internet Archive**: gemeinfreie Spielfilme, klassische Zeichentrickfilme
  bis 1963, Stummfilme und das Prelinger-Archiv.

Dreieck sucht in beiden. Eintrag öffnen, dann *Play*.

## YouTube und Crunchyroll

- **YouTube**: Suchen und Stöbern über die offizielle YouTube Data API mit
  einem **eigenen** kostenlosen API-Schlüssel (Anleitung in der App).
  Videos laufen **nicht** im App-eigenen Player – YouTube erlaubt das nur in
  den eigenen Playern. Stattdessen gibt es in den Video-Details **YouTube app**
  (startet die offizielle YouTube-App der PS5), **Browser** (öffnet das Video
  im PS5-Browser) und einen QR-Code fürs Handy. Beide Übergaben sind
  experimentell und noch nicht auf der Konsole getestet.
- **Crunchyroll**: nicht möglich. Es gibt keine öffentliche Schnittstelle,
  und die Videos sind DRM-geschützt; eine Umgehung kommt nicht in Frage. Bitte
  die offizielle Crunchyroll-App der PS5 nutzen. Der Anime-Modus zeigt
  Katalogdaten von AniList mit Links zu offiziellen Anbietern.

## Wenn die App abstürzt

Die App zeigt beim Start die einzelnen Schritte auf dem Bildschirm an und
meldet einen Absturz vor dem Fehlerdialog als Benachrichtigung, z. B. *„AKENO
STREAM 0.6.0 crashed: SIGSEGV … at eboot+0x1a2b3c … during startup: fonts“*.
Bitte diesen Text (gern als Foto) schicken. Beim nächsten Start weist die App
selbst auf den Absturz hin und zeigt ihn unter Settings -> Diagnostics.

## Wenn etwas nicht funktioniert

1. Settings -> *Diagnostics* öffnen, *Run media self-test* und
   *Play hardware test clip* ausführen.
2. *Export report* wählen und die App offen lassen; die Datei
   `akeno-diagnostics-….txt` per FTP aus
   `/mnt/sandbox/PPSA99276_000/download0/akeno/` herunterladen. Sie enthält
   keine API-Schlüssel.
3. Bericht, genaue Firmware-Version, ShadowMountPlus-Version und den im Menü
   *About* angezeigten Build-Namen im GitHub-Issue oder Pull Request posten.
