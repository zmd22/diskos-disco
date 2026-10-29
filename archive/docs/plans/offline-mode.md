> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Plan: Offline mode

Status: **on hold** (owner, 2026-10-21: not useful enough for its risk). Previously queued for 2.7.0. Our plan of 2026-10-20 merged with the "Offline mode proposal
against 2.6.1" (21 Oct). Nothing built yet. All choices made.

## Goal

"Offline in spirit": one switch after which **nothing online can be started, even by accident**. It came from a user
who never used Weather or Last.fm but kept opening them by mistake. Disco becomes a local music player.

**Bluetooth is not part of it.** Headphones, pairing, codec settings, Bluetooth DAC and BT streaming stay exactly as
they are; the switch never turns Bluetooth on or off and never disconnects anything. Not an airplane mode.

## Decided

- **Name:** "Offline mode". One persistent setting (`offline_mode`, default Off).
- **Where:** Settings > Network, first row, the same in every theme. Explanation: "Wi-Fi off. Bluetooth stays
  available. Online features are paused." Confirmations: "Offline mode on" / "Offline mode off".
- **Quick Settings Wi-Fi tile while offline:** greyed; it can't switch the radio on and says "Unavailable in Offline
  mode". Holding it points to the Offline mode setting instead of opening a Wi-Fi scan.
- **Quick Settings action:** an optional, assignable "Offline mode" action, like the other tiles. No new icon forced
  anywhere, nothing added to the Music screen.
- **Last.fm while offline:** nothing is recorded or kept for later. On entering, an already-earned listen from before
  may be finished once; then no listening is counted until Offline mode is off again. The account and the existing
  queue are kept; no logout.
- **Weather on Home and Music:** the space stays empty (layouts are not rearranged).
- **Two layers:** navigation hides the online things, **and** every online feature refuses to run on its own while
  offline, so a forgotten entry point still can't go online.
- **Off restores exactly the previous state:** Wi-Fi back on only if it was on before; every feature returns to its own
  setting; nothing that was off gets switched on; accounts, saved networks, caches, shortcuts and menu order kept.

## Engineering rules (from the proposal, agreed)

- **One policy, not changed settings.** A small `network_mode` module owns the switch. Features read "allowed?" from
  it; their own saved settings (`wifi_on`, online art/lyrics, auto-tag, weather, Last.fm) are never overwritten. Works
  after a crash, can't lose preferences, no burst of setting writes.
- **Wi-Fi is set explicitly off, never toggled.** A toggle can turn an already-off radio on. One "set Wi-Fi to X"
  path; the keep-alive, scans, connects and delayed callbacks all respect "offline" (effective Wi-Fi = saved choice
  AND not offline). Wi-Fi-specific controls only: never a blanket `rfkill block all`, never touching Bluetooth.
- **Downloaded things stay usable.** Covers and lyrics already on the Disc keep working; only new downloads stop.
  (Today the online-art switch controls both fetching and using saved online covers: they must be separated.)
- **Running work really stops:** weather, art/lyrics downloads, auto-tag lookups, Last.fm sign-in/uploads, Music
  Assistant and the Debug Mode SSH. Each job carries the policy "epoch"; late results from before the switch are
  thrown away quietly (not shown as errors). Only Disco's own processes are stopped (no `killall`). A tag rewrite
  that has already started is allowed to finish safely.
- **AirPlay / Music Assistant playing when switched on:** that receiver stops and the Disc returns to Playback; music
  doesn't start by itself. After Offline mode is off, they don't resume on their own (an explicit choice again);
  their settings are kept.
- **"Online & cache" is split:** "Online features" (greyed while offline) and "Artwork cache" (local, stays usable),
  same underlying settings.
- **Navigation:** hidden entries are filtered where each list is shown, by stable IDs; saved menus, shortcut slots and
  their order are never rewritten and come back as they were. A central screen guard: no hidden screen opens via a
  deep link or the Back history; an open Weather/Last.fm screen goes back to a valid one.
- **Automatic time / time-zone lookup** paused while offline; manual date, time and zone stay available. The Disc's
  battery-backed clock keeps time (it may drift slightly).
- **Honest switching:** "Offline mode on" only once Wi-Fi is confirmed off and Disco's online work has stopped. A
  failure is shown with a retry; networking is never silently turned back on to recover.
- **SD-card updates, USB DAC/storage, local apps, stats, EQ, lyrics files, Immersive lyrics:** all keep working.
- Note: Debug Mode SSH needs Wi-Fi, so `disco-push.sh` updates need Offline mode off (SD-card updates still work).

## What hides, what's disabled

| | While offline |
|---|---|
| Weather | hidden everywhere: app, menus, launchers, shortcuts and their pickers, Quick Settings, Home/Music/saver readings, weather hints; no fetching |
| Last.fm | hidden everywhere: app, settings, shortcuts, sign-in QR, status; no tracking, upload or sign-in |
| Music Assistant (Sendspin) | hidden from menus/shortcuts; settings disabled; helper and discovery stopped |
| AirPlay | hidden as a working mode; stopped if active |
| Wi-Fi | radio off; Network > Wi-Fi disabled and the Quick Settings tile greyed, both saying "Unavailable in Offline mode"; saved networks kept |
| Online lyrics, online art, auto-tag (incl. its Playback setting and manual lookups) | disabled with the reason; local tags, files and song details untouched |
| Debug Mode SSH, automatic time | unavailable / paused; manual clock and USB recovery stay |
| Disc Shelf (known online app) | hidden; downloaded music stays in the library |
| Bluetooth, local music, library, queue, playlists, stats, EQ, local apps, SD updates, USB modes | unchanged |

## Not in this version (decided against or later)

- **Early boot hook** to keep Wi-Fi off from the very first second: it means touching the boot scripts ("don't poke
  the bear"). Disco switches Wi-Fi off early in its own start-up instead, and we say so honestly.
- **Network labels for third-party apps** (`network=required|optional|none` in app.conf): a good idea, later. This
  version hides only the known online app.
- It's a user mode, not a security sandbox: a root-level app that reconfigures radios itself isn't prevented.

## Build order (each step its own test build)

1. Policy module, explicit Wi-Fi setter, effective-Wi-Fi rule, the Settings switch; Bluetooth verified untouched
   (headphones, BT DAC, BT streaming) on the Disc.
2. Stopping running work cleanly: weather, Last.fm, art, lyrics, auto-tag, Music Assistant, Debug Mode; rapid
   toggles, stale results, a tag rewrite in progress.
3. The three themes (Disco, Null, Squash): hidden entries, screen guard, Online features / Artwork cache split,
   disabled rows with the reason. Renders before and after for the owner.
4. Docs: How-to, tour, release notes.

## Release checks

- Music and an existing Bluetooth stream keep playing when switched on; pairing and codec stay accessible.
- Restart while offline: no Wi-Fi coming back from any path (keep-alive, Quick Settings, scans, Music Assistant, apps).
- No weather, art/lyrics lookups, tag downloads, scrobble uploads or discovery while offline (watch the processes,
  not just the screen); no repeated errors after the switch.
- Weather and Last.fm absent from every theme, menu, shortcut and Quick Settings; no way in via Back or deep links.
- Saved covers, embedded/sidecar lyrics, stats, EQ and local apps work.
- Switching off restores preferences, shortcuts and the previous Wi-Fi state; no lost account, cache or duplicate
  scrobbles; interrupted network playback doesn't resume by itself.
- SD updates, USB DAC/storage and stock-boot recovery still work.

Decisions recorded 2026-10-21 (switch in Network, greyed Wi-Fi tile, optional Quick Settings action).

Estimate: moderate. The settings row and hiding are straightforward; stopping running work cleanly and the radio
need the most care.
