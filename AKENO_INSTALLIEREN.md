# AKENO STREAM 0.4.2 – Installation (Kurzfassung auf Deutsch)

Ausführliche Dokumentation (Englisch): [README.md](README.md).

**Stand:** 0.4.1 läuft auf einer PS5 mit Firmware 13.09 und ShadowMountPlus:
Oberfläche, Controller, HTTPS, Hardware-Video (H.264) mit Ton, ein HLS-Stream,
der Anime-Katalog und der Diagnose-Export funktionierten auf der Konsole. Was
noch nicht auf der Konsole ausprobiert wurde, steht in der README. Bitte die
[Hardware-Checkliste](docs/HARDWARE_ACCEPTANCE.md) weiter durchgehen.

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
   **AKENO STREAM** starten (Firmware 12.20, kein PSN nötig).

Der Ordner enthält `eboot.bin`, `sce_sys/`, `sce_module/libc.prx` und
`assets/` (Schriften und Offline-Testvideos).

## Erster Test

Home -> *Offline Test Clips* -> **A/V Sync Test Clip**: Testbild mit
Sekundenzähler, Dauerton und einem Piepton pro Sekunde. Läuft ohne Internet.
Sind Bild und Ton synchron, funktionieren Hardware-Videodecoder und
Tonausgabe.

## Steuerung

- **L1/R1**: Modus wechseln (Home, Anime, YouTube, Library, Settings);
  im Player ±60 s spulen
- **Steuerkreuz/linker Stick**: navigieren; im Player links/rechts ±10 s,
  hoch/runter Lautstärke
- **Kreuz**: auswählen, im Player Pause/Weiter
- **Kreis**: zurück, im Player stoppen
- **Dreieck**: Suche (Anime, YouTube)
- **Quadrat**: Favorit; im Player maximale Qualität ändern
- **OPTIONS**: Dienst-Infos, im Player Stream-Informationen

## Wo Dateien hingehören

Per FTP **schreiben** kann man nur in den Installationsordner
`/data/homebrew/PPSA99276/` (die App sieht ihn als `/app0`). Den eigenen
Datenordner der App kann man vom PC nur **lesen**, und nur solange AKENO STREAM
läuft: `/mnt/sandbox/PPSA99276_000/download0/akeno/`.

| Was | Wohin (vom PC aus) |
| --- | --- |
| Eigene Videos (MP4, MKV, MOV, TS) | `/data/homebrew/PPSA99276/media/` |
| Eigene Stream-Liste | `/data/homebrew/PPSA99276/streams.json` |
| YouTube-API-Schlüssel (optional) | `/data/homebrew/PPSA99276/youtube-key.txt` |
| Diagnoseberichte abholen | `/mnt/sandbox/PPSA99276_000/download0/akeno/` (App muss laufen) |

Videos erscheinen unter *Library* -> *Media in the install folder*. Nur
Streams und Dateien verwenden, die man ansehen darf. Die YouTube-Datei nach
dem Import (Quadrat im YouTube-Modus) wieder löschen.

## YouTube und Crunchyroll

- **YouTube**: Suchen und Stöbern über die offizielle YouTube Data API mit
  einem **eigenen** kostenlosen API-Schlüssel (Anleitung in der App).
  Videos werden **nicht** in der App abgespielt – YouTube erlaubt das nur in
  den eigenen Playern; stattdessen zeigt die App einen QR-Code fürs Handy.
- **Crunchyroll**: nicht möglich. Es gibt keine öffentliche Schnittstelle,
  und die Videos sind DRM-geschützt; eine Umgehung kommt nicht in Frage. Bitte
  die offizielle Crunchyroll-App der PS5 nutzen. Der Anime-Modus zeigt
  Katalogdaten von AniList mit Links zu offiziellen Anbietern.

## Wenn die App abstürzt

Die App zeigt beim Start die einzelnen Schritte auf dem Bildschirm an und
meldet einen Absturz vor dem Fehlerdialog als Benachrichtigung, z. B. *„AKENO
STREAM 0.4.2 crashed: SIGSEGV … at eboot+0x1a2b3c … during startup: fonts“*.
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
