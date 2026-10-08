# Akeno Anime PS5 – Build & Installation (Deutsch)

## Was ist das?
Ein **Offline-Technik-Prototyp** mit eigener Akeno-Anime-Oberfläche und DualSense-Navigation.
Noch **kein** funktionsfähiger Crunchyroll-Player: Es gibt weder Crunchyroll-Anmeldung noch Katalogabfrage noch geschützte Videowiedergabe.

## Wichtig: Den Ordner PPSA99999 nicht installieren
Der Ordner `PPSA99999/` ist ein **alter, versehentlich ins Repository hochgeladener Build- und Quellcodeordner**. Seine `eboot.bin` enthält noch Hello World. Nicht erneut auf die PS5 kopieren.

Der korrekte Code wird aus dem **Repository-Hauptverzeichnis** (`src/`) gebaut. Die neue App heißt **PPSA99276** und ist unter `sce_sys/param.json` so eingetragen.

## Ohne WSL: GitHub Actions
1. Auf GitHub **Actions** öffnen.
2. Workflow **Akeno Anime PS5 Preview** auswählen.
3. Lauf für Branch `akeno` öffnen und warten, bis `build` grün ist.
4. Unten unter **Artifacts** `AKENO-ANIME-PS5-PPSA99276` herunterladen.
5. Äußere GitHub-Artefakt-ZIP entpacken, dann die darin enthaltene `PPSA99276.zip` ebenfalls entpacken.
6. Den fertigen Ordner `PPSA99276/` per FTP nach `/data/homebrew/PPSA99276/` auf der PS5 übertragen. **Keine** ZIP-Datei direkt hochladen.
7. Die alte `/data/homebrew/PPSA99999/`-Installation schließen und nach einer Sicherung entfernen, damit es keine doppelte/veraltete Anzeige gibt.
8. ShadowMountPlus neu starten bzw. die Homebrew-Liste aktualisieren, anschließend **Akeno Anime Preview** starten.

## Was sollte angezeigt werden?
Eine Bildschirmüberschrift `AKENO ANIME`, die Register `START`, `KATALOG`, `MEINE LISTE`, `KONTO`, `DIAGNOSE` und ein Hinweis `OFFLINE PROTOTYP - KEIN STREAMING`.
Kein `HELLO WORLD`.

## Steuerung
- L1/R1: Tabs wechseln
- D-Pad hoch/runter: Zeile auswählen
- Kreuz: Infodialog
- Kreis: Zurück

## Falls etwas schiefgeht
- Falls GitHub Actions rot wird: Lauf > `build` > gescheiterten Schritt öffnen und das Fehlerprotokoll teilen.
- Falls die alte Oberfläche erscheint: Prüfen, dass **genau** `PPSA99276/` aus dem neuen Actions-Artefakt kopiert wurde, und ShadowMountPlus sauber neu starten.
- Falls die neue App abstürzt: PS5-Firmwareversion (genaue 12.xx-Version), verwendeter Loader/Payload, PS5-Log und Build-Lauf mitteilen.

## Technische Basis
- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), GPL-3.0-or-later
- Firmware 12.70 von Upstream getestet, nicht jede 12.xx-Version.
- Die App muss auf der echten Konsole verifiziert werden. Erfolgreiche CI bedeutet **nur**, dass sie gebaut wurde.

**Nicht** mit einer offiziell von Crunchyroll unterstützten App verwechseln.
