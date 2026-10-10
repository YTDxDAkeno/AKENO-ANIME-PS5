# AKENO STREAM 1.0.0 – Installation (Kurzfassung auf Deutsch)

Ausführliche Dokumentation (Englisch): [README.md](README.md).

**Stand (1.0.0, Release Candidate):** Auf einer PS5 mit Firmware 12.20
(Jailbreak) liefen mit der Version vor 1.0.0: Start, DualSense-Steuerung,
natives HLS, PeerTube-Videos mit Bild und Ton, YouTube-Videos im
eingebetteten Browser, eigene Webseiten sowie die Crunchyroll-Webseite mit
Anmeldung. Crunchyroll-Folgen brechen mit **KAT-6005** ab, bei AnikotoTV lädt
der Player, startet aber nicht (siehe *Bekannte Einschränkungen* in der
README). **Neu in 1.0.0** (automatisch auf dem PC getestet, für die PS5
gebaut, noch nicht auf der Konsole): die neue **Home**-Seite, **Webseiten als
eigene Modi** (eigener Tab) und das **Playback Lab**, das jetzt auch ein mit
Clear Key verschlüsseltes Testvideo abspielt. Bitte die
[Hardware-Checkliste](docs/HARDWARE_ACCEPTANCE.md) durchgehen, besonders
Abschnitt 11. Änderungen: [CHANGELOG.md](CHANGELOG.md).

> **Rückweg:** Vor dem Update eine Kopie des alten Ordners
> `/data/homebrew/PPSA99276/` behalten. Startet 1.0.0 nicht, den alten Ordner
> zurückkopieren – Webseiten, Verlauf und Einstellungen liegen im
> Datenordner der App und bleiben erhalten.

## Herunterladen

**Release:** Auf GitHub unter **Releases** `AKENO-STREAM-PS5-v1.0.0-rc.1.zip`
und `SHA256SUMS` herunterladen, die Prüfsumme vergleichen
(`sha256sum -c SHA256SUMS`) und die ZIP entpacken: darin liegt der Ordner
`PPSA99276/`.

**Entwicklungsstand** (statt Release):

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

Home -> *Local Library* -> **A/V Sync Test Clip**: Testbild mit
Sekundenzähler, Dauerton und einem Piepton pro Sekunde. Läuft ohne Internet.
Sind Bild und Ton synchron, funktionieren Hardware-Videodecoder und
Tonausgabe.

## Steuerung

- **L1/R1**: Modus wechseln (Home, YouTube, Anime, Websites, eigene
  Webseiten-Modi, Discover, Library, Sources, Settings);
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
| Webseiten (optional) | `/data/homebrew/PPSA99276/websites.txt` (eine pro Zeile: `Name = https://…`) |
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

## Home (neu in 1.0.0)

Reihen: *Continue Watching*, *YouTube*, *Anime*, *My Websites*, *Discover*,
*Local Library*, *Recently Added Websites*, *Favorites*. Leere Reihen werden
ausgeblendet; YouTube-Trends (mit API-Schlüssel), Anime und Discover laden
nach, sobald Home angezeigt wird.

## Websites

Mit R1 bis **Websites** wechseln (zwischen YouTube und Discover).

- **Search or Enter Address** (oder Dreieck): Adresse eintippen (z. B.
  `crunchyroll.com`) – sie öffnet sich; andere Wörter werden gesucht
  (DuckDuckGo, in den Settings änderbar).
- **Add Website**: Adresse und Namen mit der Bildschirmtastatur eingeben
  (R1 = Sonderzeichen wie `:/.`). Die Seite erscheint mit ihrem Symbol unter
  *Your Websites*. **Quadrat** auf einer Seite: umbenennen, Adresse ändern,
  *Save to Home* (Reihe *My Websites*), **Pin as Mode** (eigener Tab nach
  *Websites* mit Name, Symbol oder Buchstabe, Farbe und Startseite; höchstens
  vier), privat, *Record video playback*, *Record what works*, entfernen.
- Die Seite öffnet sich im **PS5-eigenen Browser über AKENO STREAM**. Dort
  gelten die Bedienelemente des Browsers (Cursor, Scrollen, Zurück, Tastatur).
  Browser schließen = zurück in AKENO STREAM. **Den PS-Knopf im Browser
  besser nicht benutzen**; gibt es keinen Ausweg, L3 und R3 gleichzeitig
  drücken.
- Anmeldungen bleiben im Browser – AKENO STREAM sieht keine Passwörter oder
  Cookies.
- **Playback Lab** misst, was der Browser kann (MP4, MediaSource, HLS,
  Video in fremden Frames, verschlüsseltes Video mit Clear Key, DRM-Systeme,
  Vollbild, Ton) und ordnet aufgezeichnete Fehler einer Seite (z. B.
  KAT-6005) einer von acht Ursachen zu – nur so weit die Messungen reichen.

## YouTube und Crunchyroll

- **YouTube**: Videos laufen im **offiziellen eingebetteten YouTube-Player**
  innerhalb von AKENO STREAM (*Play* in den Video-Details, *Play uploads* bei
  Kanälen). Ohne API-Schlüssel: **Play a link** (YouTube-Link oder Video-ID)
  und **youtube.com** (die komplette Seite). Suchen und Stöbern brauchen wie
  bisher einen **eigenen** kostenlosen API-Schlüssel (Anleitung in der App).
  Es wird nichts aus YouTube „herausgezogen“.
- **Crunchyroll**: Home -> Reihe *Anime* -> *Crunchyroll* öffnet die echte Webseite im Browser
  innerhalb von AKENO STREAM; anmelden auf Crunchyrolls eigener Seite (AKENO
  fragt nie nach dem Passwort). Die Folgen sind DRM-geschützt und laufen nur,
  wenn der Browser der Konsole ein DRM-System anbietet – der Browser-Test
  misst das, der Abschnitt zeigt das Ergebnis. DRM wird nicht umgangen.

## Wenn die App abstürzt

Die App zeigt beim Start die einzelnen Schritte auf dem Bildschirm an und
meldet einen Absturz vor dem Fehlerdialog als Benachrichtigung, z. B. *„AKENO
STREAM 1.0.0 crashed: SIGSEGV … at eboot+0x1a2b3c … during startup: fonts“*.
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
