# Changelog

User-facing changes in diskOS Disco!. Details of each release are in [docs/releases](docs/releases/); everything else is in the Git history. diskOS is beta software.

## 3.4.2

**Cyrillic in Zune** (thanks to Mihail Dyusov, [PR #24](https://github.com/zmd22/diskos-disco/pull/24)), with Disco's
large fonts redone in a compressed format: `mq_ui` is about 680 KB smaller than 3.4.1. Battery totals restart after a
full charge again. [Release notes](docs/releases/v3.4.2.md)

- Zune: Cyrillic in all 21 font cuts (PR #24 by @meekieD).
- Fonts of 32 px and up compressed, Boomshine glyphs run-length encoded; pixel-identical renders.
- Battery: charges followed from the level, charging flag and voltage; "since full charge" restarts after a full
  charge, also one that stops below 100 % by itself; idle power-off works right after an unplug. New details screen
  with a 24-hour graph.
- Squash: Accent colour picker in Squash settings › Appearance; Orange keeps Squash's orange, anything else follows
  the shared Accent colour.
- Disc Shelf always uses the A-Z keypad.

## 3.4.1

**MA Sendspin speaks Sendspin 1.0, with code pairing, album art and FLAC** (experimental; needs Music Assistant 2.10.7
or newer). Fresh Bluetooth and Wi-Fi screens for Squash and Disco, and fixes for shuffle. [Release notes](docs/releases/v3.4.1.md)

- MA Sendspin: end-to-end encrypted (Noise; Monocypher); pairing with the 6-digit code or QR code the Disc shows, or the
  Pairing Token QR in Settings; Allow Unpaired and Forget Servers; album artwork in Music; FLAC first, PCM still offered.
- Sync Delay: 10 ms steps, − / + buttons, double-tap the slider for 0.
- Squash and Disco: new Bluetooth and Wi-Fi screens (the icon's shade says on or off, a ring that spins while scanning,
  big "Scanning…", taller rows). Disco Quick Settings bubbles sit on an oval.
- A real AirPlay icon instead of the Wi-Fi fan; Working mode shows the same icons in every theme. Sendspin on the Zune
  home dial.
- Fixed: opening Up Next on a large shuffled queue froze the Disc (a 28,619-song shuffle took it down).
- Fixed: Previous on shuffle played some other song: it now goes back to what played, and Next retraces it.
- A stall reporter in the crash log, for hangs that never end.

## 3.4.0

**aptX and aptX HD on the Disc** (experimental). [Release notes](docs/releases/v3.4.0.md) · [Bluetooth guide](docs/BLUETOOTH.md)

- aptX / aptX HD through Disco's own open-source BlueALSA build; Settings › Connectivity › aptX (experimental).
- Bluetooth Codec: SBC, AAC or aptX/LDAC Mobile / Standard / High (LDAC → aptX HD → aptX → AAC → SBC, by what each pair has).
- LDAC always on FiiO's Bluetooth audio; a pair is reconnected once when its best codec needs the other stack.
- Codec badge in Song Info; crash guard; Quiet Wi-Fi for Bluetooth (fewer dropouts).
- Fixed: headphones stuck on AAC after one slow codec switch.
- New Linux app with Fedora and Arch packages; the Windows app redesigned with tabs (both experimental).

## 3.3.1

- Fixed main Settings retaining Wheel geometry after changing List motion in Squash, Disco and Null; ordinary returns preserve scroll position.
- Corrected Braun 2.0/Disc Shelf documentation, package links and licences, and screenshot provenance.
- Fresh matching MIPS UI build. [Release notes](docs/releases/v3.3.1.md)

## 3.3.0

**Braun 2.0**, a new light and dark theme. [Release notes](docs/releases/v3.3.0.md)

- Warm panels, shaded keys with orange icons, Inter type that follows Font Size.
- Home: a wall clock with date, weather, battery and Wi-Fi/Bluetooth glyphs.
- Music: artwork beside or above the text, a radio-dial seek scale with scrubbing, an optional gesture zone.
- Quick Settings with battery and brightness scales; matching Working mode, Shortcuts, Weather, menus and EQ.
- Braun's volume dial with a high/low gain switch.
- Fixed: the colon in Braun's large times and the A–Z wheel's large letter.

## 3.2.2

- Default artwork for songs without a cover.
- Wheel lists open albums and artists with Play and Shuffle in view.
- Fixed: Queue always starts and returns to the list it interrupted; Now Playing with quotes in tags; Squash wheel pills; Zune search and shortcuts (#22).

## 3.2.1

- Smoother Wheel lists; Disco's buttons get some depth.
- Fixed: idle power-off on the charger, power-button shutdown saving stats, Queue under Wheel mode.

## 3.2.0

- Wheel lists, the QS style *Shade*, a letter wheel and A–Z keypad, round track menus, Auto-tag in Song info.
- One header line on every screen; tidier Queue, EQ, Search and volume.
- Fixed: idle power-off, vertical text scrolling, battery use while idle.

## 3.1.0

- Automatic light/dark appearance, menu-edge fades, idle timer and queue/history fixes.

## 3.0.1

- A–Z Focus wheel; Settings reorganised the same way in every theme; idle shutdown fixes.

## 3.0.0

- The **Zune** theme (by namastecoconut-imagined), Immersive 3.0, USB-C audio output in Zune, USB recovery, a new website.

## Older releases

- **2.7.0** — Shuffle! in Disco, Null and Squash; Navidrome downloads.
- **2.6.x** — Fast skipping, Bluetooth headphone handling, Help, crash log.
- **2.5.x** — Windows app, bulk queue and folder queueing, playlists, Immersive and battery improvements; Squash (2.5.0).
- **2.4.0** — The **Null** theme, clearer settings, calmer scrolling.
- **2.3.0 – 2.0.0** — Library, Queue and display additions; the Windows app can flash.
- **1.5.0** — Disc Shelf.
- **1.4.x** — Quick Settings, faster track changes, optional apps; security fixes.
- **1.3.x** — Left-handed mode, list scroll styles, album hold menu, Song info.
- **1.2.x** — Album Roulette built in, smoother scrolling, navigation drawer.
- **1.1.x** — Folder A–Z, MA Sendspin (experimental), Opus, AirPlay and Bluetooth streaming.
- **1.0** — The fork becomes **diskOS Disco!** with the Disco theme.
- **Fork 1.2.x** — SD card updates, hold menus, Ring fixes.
- **diskOS 1.2.0 and earlier** (upstream) — V2.57 support, signed updates, Books view, weather and Last.fm.
