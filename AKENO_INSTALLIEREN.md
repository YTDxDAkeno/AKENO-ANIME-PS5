# AKENO STREAM 0.4.1 – Installation (Kurzfassung auf Deutsch)

Ausführliche Dokumentation (Englisch): [README.md](README.md).

**Wichtig:** AKENO STREAM wurde gebaut und auf dem Build-Rechner getestet,
lief aber **noch nicht erfolgreich auf einer echten PS5** (0.4.0 stürzte beim
Start ab; 0.4.1 behebt die wahrscheinliche Ursache, den System-Heap). Bitte nach der Installation die
[Hardware-Checkliste](docs/HARDWARE_ACCEPTANCE.md) durchgehen und bei Fehlern
einen Diagnosebericht exportieren (siehe unten).

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

## Eigene Videos und Streams

- Videos (MP4, MKV, MOV, TS) per FTP nach `/download0/akeno/media/` kopieren
  (der Ordner entsteht beim ersten Öffnen von *Library*) und unter *Library*
  abspielen. `/download0` ist auf ca. 256 MB begrenzt.
- Eigene HLS-/TS-Links in `/download0/akeno/streams.json` eintragen (Format
  siehe README). Nur Streams verwenden, die man ansehen darf.

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

0.4.1 zeigt beim Start die einzelnen Schritte auf dem Bildschirm an und meldet
einen Absturz vor dem Fehlerdialog als Benachrichtigung, z. B. *„AKENO STREAM
0.4.1 crashed: SIGSEGV … at eboot+0x1a2b3c … during startup: fonts“*. Bitte
diesen Text (gern als Foto) und die letzte Meldung auf dem Startbildschirm
schicken.

## Wenn etwas nicht funktioniert

1. Settings -> *Diagnostics* öffnen, *Run media self-test* und
   *Play hardware test clip* ausführen.
2. *Export report* wählen; die Datei
   `/download0/akeno/akeno-diagnostics-….txt` per FTP herunterladen. Sie
   enthält keine API-Schlüssel.
3. Bericht, genaue Firmware-Version, ShadowMountPlus-Version und den im Menü
   *About* angezeigten Build-Namen im GitHub-Issue oder Pull Request posten.
