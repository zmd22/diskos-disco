# diskOS Disco! - to do

Live list. Released: 3.2.1 (tag v3.2.1). Older notes: `todo-history.md`.
How we work: owner suggests -> Claude offers fixes -> host renders for big visual changes -> owner's go ->
batch -> one test build -> a few days on the Disc -> one release; Claude hands over patch files, ChatGPT applies them.
List words: "add to the list" pins an item; "major" / "minor" sorts it; "priority" puts it first.
Links always point to releases/latest. No upstream diskOS version shown to users.

## Released: 3.2.0 (2026-10-28)
- Wheel lists, QS style Shade, letter wheel and A-Z keypad, round track menus, header line, pills, fades, idle
  power-off fix, battery work. Owner tested preview builds; the final build to be confirmed on the Disc.

## Next
- Shade following the finger while pulling (now it animates after the swipe).
- Covers in wheel list rows for albums/playlists (only the album view and artist albums have them).

## Released: 2.6.2 (2026-10-21)
- Born on the right codec: tested by hand on the owner's Disc (LDAC), automatic version covered by bt_born_test only.

## Released: 2.6.0 (2026-10-21)
- Fast-skipping work (pacing, cover settle, history after 1 s) and the crash log tested on the owner's Disc; the
  Bluetooth fixes (three audits: B01-B07, B09 part, P01, P02, N01-N04; codec LDAC -> AAC -> SBC) and the hint /
  How-to update checked with tests and renders only. Watch `/usr/data/diskos_crash.log` reports.

## Waiting on users
- #13 / Bluetooth LDAC (2.5.8: music waits for the codec switch; no device search during playback): never tested
  with LDAC/AAC headphones by us; the reporter to confirm.

## Queued for 2.7.0 (owner, 2026-10-21; build when the owner says)
- **Signing epoch:** switch the repo's `ui/tools/owner-keys/diskos-sign.sh` to a UTC timestamp to the second
  (YYYYMMDDHHMMSS; the Disc allows 18 digits). The minutes-since-2026 epoch is refused by Discs that accepted
  timestamp epochs from older scripts. The owner already uses the fixed copy.
- **`disco-push.sh` into the repo** (`ui/tools/owner-keys/`): signs with diskos-sign.sh, reads the Disc's accepted
  epoch first, copies over one SSH connection without deleting anything (the Disc's rm guard silently refuses rm on
  the card), verifies. Document in UPDATING.md next to the SD-card route.

## Polish (owner, 2026-10-20)
- **Shorter README:** move the "New in 2.0 ... 2.5" history to the changelog; keep what Disco is, how to install,
  the quick answers and the links.
- **Screenshots refresh:** tour, website and the Windows guide's install screen (no Version step yet); multi-select
  and the newer Squash/Null screens aren't pictured anywhere. Host renders, round format.
- **Consistent message wording:** the same phrasing for "try again", the same dashes, sentence case in every pop-up.
- **Two stability leftovers:** Bluetooth reconnect attempts stop when Bluetooth is turned off or another device is
  chosen (today they can overlap the delayed device search); a large "Add to Favourites" selection is written in one
  go (one transaction, off the screen's thread) instead of item by item.

## Major
- **Ratings & smart playlists** (owner, 2026-10-26; one release, after 3.2.0 has had its days on the Disc):
  1-5 star song ratings set from the track actions sheet, plus smart playlists built from ratings and listening
  stats ("most played this month", "added recently", "never played", "forgotten favourites", "4+ stars not played
  in 60 days"). Reuses existing stats data; nothing online. Renders first.
- **Dyslexic-friendly font** (owner, 2026-10-19): OpenDyslexic 3 under Display > Layout & extras, overriding text
  fonts in every theme at every size; icons, clocks, big numbers and non-Latin scripts keep their fonts. LVGL's
  runtime TTF renderer (one ~0.1-0.15 MB font file). Needs renders of all themes and a real Disc test.
- **Artwork cancellation (R07):** parked on branch `r07-art-cancel`. The decoder change itself is clean (exhaustive
  stop-point test), but the build crashed under fast skipping on the Disc. Resume only with a crash log from that
  build (the crash recorder now captures it).
- **From the 2.5.8 change specification:** R02 fully asynchronous queue operations (no database waits on the
  screen's thread; 2.5.9 only bounds them); R03 paged playlist rows, R04 compact history IDs, R05 group buffers
  sized by group count (about 18.6 MB reserved up front at 30,000 songs today); R08/R10 one decode into a shared
  output bundle and cache writes off the foreground worker; R09 reuse matched accents; R11 negative no-art cache;
  R13 -O2 experiment (measure on the Disc first).

## Minor
- Ask the owner: does Stone count as frozen like Ring and Braun, or get the Disco/Squash/Null changes (clock field)?
- Null clock: a size up would need new pixel-font sizes (its fonts are fixed sets); not done in 2.6.2.
- 2.6.2 review follow-ups: confirmed on the owner's Disc that the kernel lists Bluetooth connections as
  /sys/class/bluetooth/hci0:<n> (hci0:11 with headphones connected), so the codec fallback and the safe restarts
  work there; still to do: measure screen-off current with a stable and a flaky link; shutdown_test.py no longer builds (missing
  stats_flush stub), like transport_test.py.
- From the Bluetooth audit and the delayed-pause report, not done yet: richer command logging (origin, queued vs
  delivered, telemetry age) for the next device trace; B08 a persistent pairing agent (check on the Disc first whether FiiO's
  software already provides one; first pairings seem to work); B09 device details/forget off the screen's thread
  (they can still hold the screen up to 4-5 s); B06 confirm which codec was really selected before releasing the
  hold; remove the unused legacy bt_codec_apply_async(); ui/tests/transport_test.py no longer builds (stale harness).
- Korean/CJK in a second, larger size for Music titles (about +0.4 MB per script).

## Experiments
- **WinUSB Windows installer:** upstream's native 1.2.1 Windows installer repacked with the Disco payload (no WSL,
  no usbipd; upstream's key removed). Owner to test on the laptop; if it works, offer it as the recommended Windows
  download and build it with each release.

## Decided against
- **Offline mode** (on hold, owner 2026-10-21: "I don't think it'd actually be useful"). Wi-Fi off already keeps the
  Disc offline; the real complaint (opening Weather/Last.fm by accident) is clutter, and the full mode would touch
  many modules for little gain. The complete, decided plan stays in `docs/plans/offline-mode.md` if it ever comes back.
  Lighter ideas noted for later: Weather out of the default shortcuts; an app.conf "needs network" label so such apps
  show greyed "Needs Wi-Fi" whenever there's no network.
- Boot-script changes (restarting into Disco after a crash, upstream's S97 fixes): "don't poke the bear".
- A separate settings file for Disco vs upstream diskOS: too much headache; the docs explain the leftovers instead.
- Translation catch-up for messages: translations are a courtesy.
- A "+3" badge for waiting skips, a higher skip cap, a shorter cover settle (0.3 s matches the skip pacing).
