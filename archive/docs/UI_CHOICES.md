> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# UI choices

[Back to the README](../../README.md)

Why this fork looks and behaves the way it does — including the bits that aren't obvious from the screen.
The Disc is a **360 × 360 round display**, a single-core-feeling MIPS chip and a finger. Most decisions
fall out of those three facts.

---

## Themes

### One theme = the whole interface
A theme is not a colour scheme. Picking **Zune**, **Disco**, **Squash**, **Null**, **Ring** or **Braun** changes Home, Now Playing, the screensaver,
popups, toasts, the equalizer and every list together. Switching restarts the UI; that's deliberate —
it keeps the running interface simple and fast instead of re-skinning live.

### One Display menu for every theme
Since 2.5.4 every theme has the same Display menu in the same order. A setting that doesn't apply to the current
theme stays visible but dimmed, and tapping it says why. That way a page never looks different from the how-to,
and switching themes never loses a choice: each theme keeps its own saved preferences.

### Zune (3.0.0)

The original theme is by [namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune). Its type-led home menu, deep black and personal accent bring a different personality to the same circular display. zmd22’s pass balances large headings against readable detail, gives track/artist text more space and integrates shared controls. The fixed four-square shortcut avoids font-dependent missing glyphs; its three gestures remain independently configurable. The unused Now Playing style selector is hidden in this theme.

Immersive 3.0 shares the transport overlay and spinning-disc transition with the other modern themes. Volume captures touches above Music to prevent accidental seeking. [Theme guide](../../docs/themes/zune.md).

### Disco (the default)
- **The Disc looks like a CD, so the interface does too.** The playing album's cover fills the screen, sharp on
  Music and blurred and tinted behind everything else; a faint rainbow CD sheen runs round the rim (it can be
  turned off).
- **One navigation circle instead of menus.** It slides out of the right edge (or the left, for left-handers),
  leaving the opposite edge free for the back swipe. Its sections can be reordered, added and removed; Music and
  Settings always stay.
- **Arcs follow the rim.** Transport buttons, progress (Line or Arc) and Quick Settings sit on curves concentric
  with the screen, so nothing gets clipped by the round edge.
- **Text always readable.** Colours taken from the cover are darkened on light screens and lightened on dark
  ones, and title/artist get a soft shadow, so they never disappear into a pale cover.

### Squash
- **Big, bold, made for touch.** Large Nunito Bold text, charcoal or off-white with orange accents and outlined
  round controls. It's the theme for reading at arm's length.
- **The track text is the control.** Double-tap it to play or pause, swipe across it to skip, tap it for track
  actions. That leaves room for the text itself instead of a row of small buttons.
- **A launcher that rises from the bottom.** Tapping D! raises a large circular wheel with one big item at a time;
  drag, let it snap, tap. Bigger targets than a list, and it reuses the same menu order as Disco.
- **A quiet volume arc at the bottom**, with no extra buttons or dimming, so the screen stays calm.

### Null
- **Pixel style, Nothing-phone inspired.** Pixel fonts, a dithered cover (or full colour), squared shapes and a
  few strong colour pops. Palettes include cyan/yellow, red/lime and Matrix.
- **Always dark and right-handed.** The look depends on a black background and a fixed drawer, so those choices
  are dimmed rather than half-supported.
- **Hold to seek with a preview**, like Disco: the time follows your finger and the song only jumps when you let go,
  so a slip doesn't lose your place.

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
- **Braun Dark** (charcoal body, aluminium knobs) and the **Light / dark > Auto** option for day/night.
  The volume popup uses the theme's own surfaces in Dark, so it no longer flashes a light band at night.

### Upstream themes
The theme list shows **Disco, Squash, Null, Ring, Braun and Stone**. Upstream's other presets were hidden at
first and removed in 2.5.1 to keep the interface smaller; a Disc that still had one selected switches to Disco.

---

## Navigation

| Gesture | Does |
|---|---|
| Swipe up from the **bottom edge** | Home, from anywhere (bottom 40 px, twice the normal travel — hard to trigger by accident) |
| Pull down from the top | Quick Settings |
| **Hold** an album / artist / genre / folder | Actions: Play · Shuffle · Add to queue · Playlist |
| Tap the album art on Now Playing | Immersive record |

**Why long-press opens a menu instead of playing:** on a round screen a mistimed hold is easy, and
silently replacing your queue is the worst outcome. A menu costs one tap and is never destructive.
The lists carry a small hint: "Hold an item for more".

**Curved lists.** Library lists (Songs, Albums, Artists, Genres) follow the screen's curve like the
History list. Rows near the top and bottom step inwards so text never runs under the bezel. An A–Z pill at
the bottom jumps to a letter without pushing the list off-centre.

**Left-handed mode mirrors places, not reading.** Side = Left moves the navigation circle and every screen
arranged around it to the left, and the back swipe to the right edge, so the hand that holds the Disc can reach
the circle. Text keeps reading left to right and arcs keep their direction: progress is clockwise for
everyone. The change restarts the interface (Apply), so every screen is rebuilt in its new place.

**Three scroll styles.** Classic narrows rows symmetrically with the round screen. Straight keeps them flat
for people who find curves busy. Disco curves only the edge opposite the circle, as if the rows were spokes of
a CD turning round its hub, and keeps the times in one straight column so they stay easy to compare.

**Settings in short pages.** Display and System had about twenty rows each, with four in view. They are now
split into five or six topic pages each, so the most scrolling you ever do is a little.

**A welcome when nothing plays.** An empty Music screen gave no hint what to do. It now shows a generated CD
and "Go to Library", which is where you'd go next anyway.

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
See [UPDATING.md](../../docs/UPDATING.md).

---

## On hold

- A Winamp-style theme and a 90s cassette theme: ideas only. (Null was shelved once, then finished and ships
  since 2.4.)

## Disco 1.3.1 and 1.4.0

**Readable and tactile.** Song Info is an album back sleeve. Action menus, Shortcuts and Quick Settings use
larger targets, with simple pressed feedback instead of animations. Heavier Music fonts are optional and
pre-rendered, so nothing is drawn at runtime.

**Quick Settings, two ways.** *List* uses the same rows and curve as the Library, stopping short of the music disc; *Arc* places eight
round buttons around the music disc. You choose and order the buttons. Wi-Fi and Bluetooth keep fixed places so
your thumb learns them.

**Clear of the back swipe.** Back starts within 64 px of the edge, so buttons start beyond it. A swipe that
begins on a button never presses it.

**Shortcuts on the open circle.** Double tap and Hold on the open navigation circle run an action you choose.
Choosing Nothing for Double tap removes the short wait a double tap needs.

**Hints that retire.** First-use hints appear one at a time, vanish on touch, and stop once you've used the
feature or after a few healthy starts. Help can show them again.

**The screen never waits for the disk.** Covers, album colours and EQ presets are prepared in the background,
and large covers fall back to a lighter path when memory is low. Library scroll memory lives in RAM only.
