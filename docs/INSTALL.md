# Install, update and roll back AKENO STREAM

For a jailbroken PS5 (tested on firmware 12.20) with
[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) and an FTP
server (for example etaHEN's). No PC build environment or WSL is needed: the
release ZIP holds the finished app folder.

| | |
| --- | --- |
| Title ID | `PPSA99276` (unchanged since the first preview) |
| Install folder | `/data/homebrew/PPSA99276/` |
| Your data | `/download0/akeno/` inside the app (read it over FTP at `/mnt/sandbox/PPSA99276_000/download0/akeno/` while AKENO runs) |
| Data backup | `/data/akeno-backup/` (written by the app at every start, 1.0.0 and later) |

## New installation

1. Download `AKENO-STREAM-PS5-<version>.zip` and `SHA256SUMS` from the
   [Releases page](https://github.com/YTDxDAkeno/AKENO-ANIME-PS5/releases).
2. Check the download: `sha256sum -c SHA256SUMS` (Linux/macOS/WSL) or
   `Get-FileHash .\AKENO-STREAM-PS5-<version>.zip -Algorithm SHA256`
   (PowerShell) and compare with the value in `SHA256SUMS`.
3. Unzip it on the PC. It contains one folder, `PPSA99276/`, with
   `eboot.bin`, `sce_sys/`, `sce_module/` and `assets/`.
4. Copy the whole `PPSA99276` folder over FTP to `/data/homebrew/` so that
   `/data/homebrew/PPSA99276/eboot.bin` exists. Do not upload the ZIP.
5. Let ShadowMountPlus scan (it watches its scan paths; or press **Scan** in
   its web interface at `http://<PS5 address>:10101/`). The home screen shows
   **AKENO STREAM** with the AKENO icon.

## Update an existing installation

1. Close AKENO STREAM (PS button -> close the app).
2. **Rollback copy**: over FTP, copy `/data/homebrew/PPSA99276/` to your PC
   (or rename it on the console to `/data/homebrew/PPSA99276-previous`
   *outside* the folder ShadowMountPlus scans - e.g. `/data/akeno-previous/` -
   so it is not registered as a second app).
3. Download and verify the new ZIP (steps 1-3 above).
4. Copy the new `PPSA99276` folder over `/data/homebrew/PPSA99276/`,
   replacing the files. Your own files there (`media/`, `websites.txt`,
   `anime-catalog.json`, `streams.json`, `youtube-key.txt`, `akeno-backup/`)
   are not in the ZIP and stay untouched.
5. Start AKENO STREAM. The notifications say "AKENO STREAM <version>
   starting" and "... ready"; Settings shows the version at the bottom right.

Your settings, watch history, favorites, websites, sources, anime providers
and watchlist live in the app's data folder, not in the install folder: an
update never touches them.

## The home screen still shows "Akeno Anime Preview" or the old icon

**Why:** ShadowMountPlus registers a title once. It copies the name, icon and
backgrounds from `sce_sys/` into the system (`/user/appmeta/PPSA99276/` and
the system's app database) at the first registration, and later scans skip a
title that is already registered from the same folder (see
`sm_scan.c`/`sm_install.c` in ShadowMountPlus). The first AKENO builds
(0.1-0.3) were registered as "Akeno Anime Preview" with the orange icon;
copying newer files over the folder does not change that registration, even
though `sce_sys/param.json` has said "AKENO STREAM" since 0.4.0.

**Fix (once):** register the title again. With ShadowMountPlus 1.7:

1. Update to AKENO STREAM 1.0.0 (or later) and **start it once**. It saves a
   backup of your data to `/data/akeno-backup/`; *Settings -> Back up my
   data* should read "N files in the backup". For extra safety, also copy
   `/mnt/sandbox/PPSA99276_000/download0/akeno/` to your PC over FTP while
   AKENO is running.
2. Close AKENO STREAM.
3. Open ShadowMountPlus's web interface: `http://<PS5 address>:10101/`.
4. On the AKENO STREAM (PPSA99276) card choose **Uninstall** - the
   confirmation reads "The source image or folder will not be deleted".
   **Do not** use *Delete source*: that permanently deletes
   `/data/homebrew/PPSA99276/`.
5. Press **Scan**. ShadowMountPlus registers the folder again and reads the
   current name and artwork: **AKENO STREAM** with the new icon.
6. Start AKENO STREAM. If the system removed the old data folder with the
   registration, AKENO restores it from `/data/akeno-backup/` before anything
   loads and shows "Your data was restored from /data/akeno-backup".

Without the web interface (older ShadowMountPlus): make the rollback copy and
the data backup first, delete the AKENO tile from the home screen
(OPTIONS -> Delete), check over FTP that `/data/homebrew/PPSA99276/` is still
there (put it back from your copy if not), then restart ShadowMountPlus.

If the data was not restored automatically: copy the files you saved from
`download0/akeno/` (or `/data/akeno-backup/`) into a folder
`/data/homebrew/PPSA99276/akeno-backup/` and choose *Settings -> Restore my
data*, then close and reopen AKENO STREAM.

The PS5's own wallpaper and theme are system settings; AKENO changes only its
own tile, its selection background (`pic0.dds`), its launch background
(`pic1.dds`) and its music (`snd0.at9`). The Shell may cache artwork until the
title is registered again or the console restarts.

## Duplicate tiles

- A tile named **Hello World** or a folder `/data/homebrew/PPSA99999/` comes
  from the very first test build: delete that folder over FTP and uninstall
  that tile.
- Two AKENO tiles mean two folders with the same title ID in different scan
  paths (for example a copy on USB). Keep only `/data/homebrew/PPSA99276/`;
  ShadowMountPlus shows a "duplicate titleId" notification in this case.
- Keep rollback copies outside all scan paths (`/data/homebrew`,
  `/data/etaHEN/games`, `/mnt/usbN/homebrew`, the root of USB drives).

## Roll back

1. Close AKENO STREAM.
2. Copy your rollback copy back to `/data/homebrew/PPSA99276/` (replace the
   files; delete files the newer version added if you want an exact copy).
3. Start the app. Data files written by a newer version are read by older
   versions where the format is the same; unknown or damaged files are
   moved aside as `*.corrupt` and defaults are used, never a crash.

## Check the download and the installation

- The release's `SHA256SUMS` lists the ZIP's SHA-256. The GitHub release page
  shows the same value.
- After installation, *Settings -> Diagnostics -> System* shows the version,
  the build, the firmware and the data folder.
- *Settings -> Diagnostics -> Export report* writes
  `akeno-diagnostics-<time>.txt` to the data folder (read it over FTP at
  `/mnt/sandbox/PPSA99276_000/download0/akeno/` while AKENO runs). It never
  contains passwords, cookies or API keys.
