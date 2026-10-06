# UI choices

[Back to the README](../README.md)

Why this fork looks and behaves the way it does — including the bits that aren't obvious from the screen.
The Disc is a **360 × 360 round display**, a single-core-feeling MIPS chip and a finger. Most decisions
fall out of those three facts.

---

## Themes

### One theme = the whole interface
A theme is not a colour scheme. Picking **Ring** or **Braun** changes Home, Now Playing, the screensaver,
popups, toasts, the equalizer and every list together. Switching restarts the UI; that's deliberate —
it keeps the running interface simple and fast instead of re-skinning live.

### Ring
- **The progress ring is the interface.** Home puts the clock inside the playing track's ring, with the
  cover riding along it. Now Playing wraps the cover in the same ring — **drag the ring to seek**.
- **Two colours, two jobs.** The *interface accent* (red on Ring, lime `#A6E22E` on Ring Light) marks
  things you can press: every real button gets a 2 px accent ring. The *album colour* is for the music —
  glyphs, play/pause, title. You can always tell "control" from "content".
- **Heart left, play mode right**, both on the ring's horizontal apexes; queue below play mode.
- **Bigger type:** title 28 px, artist 20 px — matched to Braun so the two themes feel equally legible.
- **Tap the cover** → immersive record. **Tap the title/artist** → the album, with the current track focused.

### Braun
- Borrowed from the Braun **SK4** radio: warm white body, speaker-grille dot field above, a solid panel
  following the round edge below, **one orange** for focus and "on".
- **ON/OFF knobs instead of lamps.** A toggle is a knob: pointer straight up in the icon colour = off,
  rotated **45° and orange** = on. No indicator lamp — the pointer *is* the state, like a real dial.
- **Lists stay straight** (unlike Ring's curves) — it's a radio, not a watch.
- **Braun Dark** (charcoal body, aluminium knobs) and an **Automatic Appearance** toggle for day/night.
  The volume popup uses the theme's own surfaces in Dark, so it no longer flashes a light band at night.

### Hidden upstream themes
Upstream ships eight presets. The theme list shows **Ring, Braun, Stone** only. The others are still
compiled in and keep working if they were already selected — they're hidden, not removed, so nothing
breaks after an update.

---

## Navigation

| Gesture | Does |
|---|---|
| Swipe up from the **bottom edge** | Home, from anywhere (bottom 40 px, twice the normal travel — hard to trigger by accident) |
| Pull down from the top | Quick Settings orbit |
| **Hold** an album / artist / genre / folder | Actions: Play · Shuffle · Add to queue · Playlist |
| Tap the album art on Now Playing | Immersive record |

**Why long-press opens a menu instead of playing:** on a round screen a mistimed hold is easy, and
silently replacing your queue is the worst outcome. A menu costs one tap and is never destructive.
The lists carry a small hint: "Hold an item for more".

**Curved lists.** Library lists (Songs, Albums, Artists, Genres) follow the screen's curve like the
History list. Rows near the top and bottom step inwards so text never runs under the bezel. The A–Z
button sits on the right edge so it doesn't push the list off-centre.

**The EQ dial owns its touches.** While your finger is on the dial, page swipes are ignored — no more
leaving the equalizer by accident while turning a band.

---

## Queue

The stock player doesn't refresh a loaded queue when songs are added, so **Add to queue rebuilds it**.
Queued songs play **first-in, first-out** after the current song; shuffle and repeat pause while they play, then
whatever you were listening to resumes. There is no separate "Play next" — one rule, easy to predict.

---

## Covers and performance

### Covers decoded in-process
Upstream decodes artwork in a short-lived helper (`diskos-artdec`) with a safety lock. After quick skips
or album hopping the lock could leave the helper un-started, and covers stopped for good. This fork
**decodes inside `mq_ui`** (`ui/artdec_inproc.c`), with per-run state reset and allocation tracking so a
broken image can't leak or wedge it. The helper binary is still shipped for compatibility; the UI doesn't run it.

### Immersive record
- Paced on the **whole** frame time (render + compose + flush), so it spins at a steady rate instead of
  stalling at 1 fps.
- Lyrics, hint and progress ring are **baked into the frame** from snapshots taken only when they change,
  instead of being redrawn on every rotation.
- The rotation loop has no per-pixel bounds tests (same output, fewer instructions).
- **Nothing is drawn while the screen is dimmed or off.**

### Theme pass skipping
The theme kit restyles new objects on each frame. A counter in the vendored LVGL (`lv_obj_class.c`)
lets it skip frames where nothing was created and the screen and accent haven't changed — a cheap win
on every screen. *If you update LVGL, carry this one-line patch over.*

---

## Power and screen

- **Hold the power key** → themed "Shutting down / Saving…" screen while the player flushes.
  The UI reads the key directly (GPIO via `/dev/mem`) because the stock event never reaches it in time.
- **Brightness** is restored after screen off → on (stock reset it to default).

---

## Updates

Upstream's Wi-Fi updates trust upstream's release key. This fork **removes that key** and bakes in
**yours**, so the Disc accepts only builds you signed — via the SD card. Why the SD card and not SSH:
the boot script verifies `mq_ui` against a read-only manifest, so hand-copying a binary gets quarantined
and the Disc falls back to stock. Signed updates go through the proper trial-and-rollback path instead.
See [UPDATING.md](UPDATING.md).

---

## On hold

- **Glass** — a third theme with a navigation hub (Music · Library · Settings · Modes · Shortcuts · EQ),
  configurable hub items and a title-tap action overlay. Mocked up, not built.
- **Null** (Nothing-phone pixel style) and a 90s cassette theme — shelved after testing.
