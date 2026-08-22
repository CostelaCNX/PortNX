# Changelog

## [2.1.0] — 2026-08-21

Reliability release for stream installs on unstable connections.

### Fixed
- Installs now retry and resume from the last written offset when the connection drops, instead of failing the whole package. Previously only NSZ recovered from a network blip; plain NSP restarted from zero and lost all progress.
- A transfer that reports success but delivers a short body is retried instead of being accepted as complete.
- Running out of SD card space now reports how much is needed and how much is free, instead of a generic install error.
- Tickets are no longer skipped when a package ships fewer certificates than tickets. Every ticket is attempted, and one that cannot be imported is reported in the queue instead of failing silently.
- Content metadata is written only after every file is registered, so an interrupted install no longer leaves an orphan record behind.
- Leftover placeholders from installs interrupted by a crash or shutdown are cleared at startup, reclaiming SD card space.

## [2.0.1] — 2026-07-27

Maintenance release focused on installation reliability and large remote packages.

### Fixed
- Reject HTTP servers that ignore Range requests during stream installs, preventing invalid package parsing after redirects or incompatible mirrors.
- Report clearer errors when remote package headers cannot be fetched, including archive/mirror Range incompatibility.
- Improve resumed downloads by validating existing `.part` files, checking only remaining free space, and finalizing already-complete partial downloads.
- Use large-file seek/tell APIs while installing local content, improving compatibility with larger ports.

### Changed
- Remove the force reinstall setting from the app UI.

---

## [2.0.0] — 2026-07-03

Complete UI rewrite. Migrated from Borealis to Plutonium SDL2.

### Added
- Home screen with three SNES-palette cards (Browse / Queue / Settings)
- 3×2 port grid browser with 256×256 icon preview and L/R pagination
- Full-width list view in Browse, toggle with Y; Up/Down wraps between pages
- Analog stick navigation (left stick + right stick) on home and grid
- Touch support: tap cards on home, tap cells in Browse, tap top bar to go back

### Changed
- Queue tab shows translated status strings (downloading, installing, done, error)
- Cancel (X) skips only the current item and auto-starts the next in queue
- Canceled items can be re-added from Browse immediately

### Removed
- Borealis-based UI components (DownloadView, InstallView, TitlesTab, GlassListItem)

---

## [1.0.0] — 2025-06-01

Initial release.

- Browse remote port catalog over HTTP/HTTPS
- Stream-install supported packages via NCM + ES
- Install queue with per-item progress
- Encrypted catalog index support
