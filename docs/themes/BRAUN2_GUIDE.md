# Braun 2.0 — Theme Guide

A complete brief for building Braun 2.0 in diskOS Disco: design tokens, components, every
screen, implementation order, acceptance checks, and copy-paste prompts for each subscreen.

Read alongside the repo's [themes.md](../../themes.md) (theme contract) and
`docs/DESIGN_SYSTEM.md`. Visual references: `braun2_01` … `braun2_09` in this folder.

---

## 0. Decisions to make first

| Decision | Recommendation | Why |
|---|---|---|
| New preset or replace Braun? | **New preset `braun2`, appended** (`THEME_PRESET_BRAUN2`) | themes.md protects existing themes; saved IDs must not renumber. Classic Braun stays untouched. |
| Code reuse | Reuse `braun.c` helpers (`br_face`, `br_knob`, `br_segment`, `br_clock_face` …) through a `th_braun_family()` check; add `br2_*` only where geometry differs | One vocabulary, no duplicated playback/BT/Wi-Fi logic |
| Home layout | **B · Ring status** default; **A · Full-face clock** as an option | B is the most readable; A keeps the wall-clock identity |
| Music layout | **A · Record** default; **C · Type first** as an option (and automatic fallback when there is no artwork) | Native to the circle; C covers readability |
| QS layout | **B · Four keys + volume arc** default; **C · Switch list** as an option | Big targets; C for maximum legibility |

Everything below assumes these. Swap freely; the tokens and components do not change.

---

## 1. Brief

> Less, but better — and bigger. A Dieter Rams instrument on a round face: warm neutral
> surfaces, honest controls that look like what they do, one orange accent that means only
> *on / primary / now*, and type large enough to read at arm's length.

**Principles**

1. **One accent.** Orange marks: the primary key, the live needle/progress, lamps that are on,
   the clock hub and second hand. Never decoration, never a background.
2. **Controls are objects.** Knobs (discs with a rim), keys (rounded rectangles), slide
   switches, lamps, tick scales. No ghost buttons, no icon-only text links.
3. **The circle is the layout.** Rims carry status (battery, progress, volume) as arcs and
   tick scales. Text stays in the middle; controls follow the lower arc where thumbs land.
4. **Readable first.** Fewer items per screen, larger type, truncation over scrolling.
5. **Calm.** No gradients except artwork. Motion is short and mechanical (needles sweep,
   switches slide); nothing bounces.

---

## 2. Design tokens

### 2.1 Colour

Map onto existing semantic roles (`THEME_CLR_*`) in a new `FORK_BRAUN2[2][…]`, initialised
from `FORK_BRAUN` and then overridden.

| Token | Role(s) | Light | Dark |
|---|---|---|---|
| Canvas | `CANVAS` | `#EEEAE2` | `#1E1D1B` |
| Panel | `SURFACE_STRONG`, segments, pill rows, key bodies | `#E2DCD1` | `#2B2A27` |
| Panel 2 | `SURFACE_RAISED`, `CONTROL_TRACK`, switch off-track | `#D6CFC3` | `#363531` |
| Rule | `BORDER` | `#C9C2B6` | `#45433F` |
| Grille dot | `DOT_GRID` | `#D3CDC2` | `#2A2926` |
| Text | `TEXT_PRIMARY` | `#1C1B19` | `#EEEAE2` |
| Text 2 | `TEXT_SECONDARY` | `#66625B` | `#A8A39A` |
| Text 3 | `TEXT_DISABLED`, unlit ticks | `#9A958C` | `#726E67` |
| Knob | `CONTROL_KNOB` | `#2B2A27` | `#E6E1D8` |
| Knob rim | (inner ring on knobs) | `#45433F` | `#C9C3B8` |
| On knob | `ON_CONTROL` (icon on a knob) | `#F2EFE8` | `#1E1D1B` |
| Accent | `ACCENT_PRIMARY/EMPHASIS/IDLE`, `SWITCH_ON` | `#E85A16` | `#EF6A24` |
| On accent | `ON_ACCENT` | `#FFFFFF` | `#FFFFFF` |
| Lamp off | (unlit lamp) | `#B9B2A6` | `#4A4844` |
| Pressed | `SURFACE_PRESSED` | Panel −6% L | Panel +6% L |
| Accent pressed | `PRIMARY_PRESSED` | `#C84C12` | `#D85A18` |

Contrast targets: Text on Canvas/Panel ≥ 7:1; Text 2 ≥ 4.5:1; Text 3 is **never** used for
information the user needs (only unlit ticks, hints, placeholders).

### 2.2 Type (Inter — already in the tree as `ui/fonts/nowplaying/Inter-VF.ttf`)

| Role | Size / weight | Used for |
|---|---|---|
| Display | 64–72 Bold, tabular digits | Home time, volume value |
| Numeric L | 44 Bold | Music elapsed (Type-first), EQ value while dragging |
| Title | 22–24 Bold (32 on Type-first Music) | Track title, screen titles |
| Row | 19–20 SemiBold | List rows, settings, QS switch rows |
| Body | 14–16 Medium | Artist, values, secondary lines |
| Caps | 12–14 SemiBold, +1.6 px tracking, uppercase | Date, section labels. Never essential-only. |
| Micro | 13 SemiBold | Times next to progress, key captions |

Minimums: primary ≥ 18, secondary ≥ 14, caps ≥ 12. Keep the existing international
fallback chain behind every Inter size (`TF` roles).

### 2.3 Geometry

| Token | Value |
|---|---|
| Screen | 360 × 360 round |
| Text-safe circle | Ø 300 (r 150) — all text inside |
| Control-safe circle | Ø 344 (r 172) — key edges, arcs and ticks may reach here |
| Rim arcs | r 168, 5 px, round caps |
| Rim tick scale | r 166–172; major 12 px / 2.4 w, minor 6 px / 1.2 w |
| Primary key | Ø 56–68 (r 28–34) |
| Knob | Ø 44–56 (r 22–28); inner rim at 82% radius, 1.4 px |
| Lamp | Ø 10 (r 5); on a knob: r = 0.13 × knob r at 45° top-right |
| Key (rect) | 96 × 80, radius 26 |
| Pill row | height 56, radius 28; row pitch 62 |
| Segment (lower panel) | straight top, radius 34 into the rim |
| Header cap | solid Panel from the top down to y 64–76, radius 34 at its lower corners |
| Slide switch | 58 × 32, knob Ø 24 |
| Spacing scale | 4 · 8 · 12 · 16 · 24 · 32 |
| Touch target | ≥ 44 × 44 effective (use `ext_click_area` for small glyphs) |

### 2.4 Elevation & texture

- One shadow only: keys and knobs get a soft drop (y +8% r, 10% black light / 35% dark).
- Grille dots (pitch 9, Ø 2.8) appear **only** behind clocks and in QS backgrounds — never
  under body text. Option to turn the grille off.

### 2.5 Motion

| Event | Motion |
|---|---|
| Needle / progress | Linear sweep, 150 ms; position updates ≤ 1×/s otherwise |
| Switch | Knob slides 120 ms ease-out; track colour cross-fades |
| Key press | Darken to Pressed + shadow shrinks, 60 ms; release 90 ms |
| Lamp | Snap on (no fade) — it's a lamp |
| Screen change | Existing shared transition; no theme-specific flourishes |

---

## 3. Components

Build each once in `braun2.c` (or extend `braun.c`), then use everywhere.

| Component | Spec | States |
|---|---|---|
| **Primary key** `br2_key_primary` | Accent disc, white glyph 0.9 × r, faint inner ring 86% r @ 14% white | normal, pressed (Accent pressed), disabled (Panel 2 + Text 3 glyph) |
| **Knob** `br_knob` (reuse) | Knob disc, inner rim, glyph in On-knob colour | normal, pressed, on (lamp lit) |
| **Lamp** `br2_lamp` | Ø 10 disc | on = Accent, off = Lamp off. Always paired with text or position, never colour alone |
| **Key** `br2_key` | Panel rounded rect, glyph 26 px, caption 14 SemiBold below, lamp top-right | on caption = Text, off = Text 2 |
| **Pill row** `br2_row` | 56 tall, radius 28, Panel fill only when focused/selected; lamp at left, label Row, value Body Text 3 right-aligned, chevron if it opens a page | rest, focused, pressed |
| **Slide switch** `br_style_switch` (update) | Track 58 × 32 Accent/Panel 2, knob white | on / off / disabled |
| **Dial scale** `br2_dial` | Straight tick scale, majors every 5, lit ticks Text, unlit Text 3, Accent needle 4 × 30 | draggable (seek), read-only |
| **Rim scale** `br2_rim_scale` | Same as dial but along an arc (centre, r, start°, end°) | draggable |
| **Rim arc** `br2_rim_arc` | 5 px arc, track Text 3 @ 50%, fill Text (battery) or Accent (progress) | charging: fill pulses once on plug-in |
| **Step keys** `br2_steps` | N rounded 18–28 px squares, filled = Text, current = Accent, rest = Panel 2 | tap a step to set |
| **Segment / header cap** `br_segment` (reuse) | Panel with radius 34 | — |
| **Toast** | Panel pill, Row text, optional lamp; bottom-centre above the segment | info / success (lamp on) / error (lamp off + text) |
| **Popup / sheet** | Canvas card radius 34, title Title size, max 3 actions as pill rows | — |

---

## 4. Screen-by-screen spec

`CORE` = must exist; `REACHABLE` = must have a route; `DECORATIVE` = optional
(see themes.md §3). Every screen: header cap with Title + back knob (bottom-centre,
r 22) **or** the shared back gesture, never both competing.

### 4.1 Home (`SCR_HOME`) — B · Ring status

- Rim: battery arc top (−40°…40°), "72%" Micro below it; track-progress arc bottom (140°…220°) in Accent.
- Caps line: `THU 30 · 14° SUNNY` (tap → Weather).
- Time: Display 64, tabular. 12/24 h follows the shared setting.
- Track: Title 20 + Body 15; tap → Now Playing. Empty state: "Not playing" in Text 3, play key still starts last queue.
- Keys: Library knob (8 o'clock, r 24), Primary play/pause (r 32), Search knob (4 o'clock, r 24).
- Option **A · Full-face clock**: rim minute ticks, hands, date caps above centre, weather pill at 3, title pill, keys on lower arc.
- Shortcuts / hints: keep shared home shortcuts and hint placement inside the safe circle.

### 4.2 Now Playing (`SCR_NOWPLAYING`) — A · Record

- Round artwork Ø 160, centre hole in Canvas.
- Progress: rim scale r 120, −135°…135°, Accent needle; drag to seek (CORE seek).
- `2:52 / 7:11` Micro, title Title 20, artist Body 14.
- Prev / Primary / Next along the lower arc.
- REACHABLE: favourite, sleep, mode (shuffle/repeat) → NP menu (`SCR_NPMENU`) via long-press on the title or a small knob; never remove without a route.
- No artwork → automatically use **C · Type first** (32 px title, 44 px elapsed, straight dial).
- Lyrics/queue: swipe up → `SCR_NPHUB` as today.

### 4.3 Quick Settings (`SCR_QUICK`) — B · Four keys + volume arc

- Caps status line: time · battery.
- 2 × 2 keys (96 × 80) for the user's first four QS items; items 5–6 on a second page (page dots).
- Volume: rim scale along the bottom (128°…232°), Accent needle, value Display-ish 30 Bold + "VOLUME" caps.
- Brightness: long-press volume label to switch the arc to brightness (label changes), or put brightness as a step-key row on page 2.
- Long-press a key → its detail (BT/Wi-Fi) as today. QS editor (`SCR_QSCONFIG`) must agree on capacity (6).
- Option **C · Switch list**: four switch rows inset to the circle + step-key volume.

### 4.4 Lists — Library, Albums, Artists, Folder, Playlists, Queue, Up Next, Search results
(`SCR_LIBRARY`, `SCR_ALBUMWALL`, `SCR_FOLDER`, `SCR_PLVIEW`, `SCR_PLPICK`, `SCR_QUEUE`, `SCR_UPNEXT`, `SCR_SEARCH`, `SCR_SETLIST`, `SCR_BOOKS`, `SCR_CHAPTERS`)

- Header cap + Title. Rows = pill rows, 3–4 visible; focused row gets Panel fill + lit lamp.
- Rows narrow toward top/bottom (follow the curvelist's existing curve).
- Playing track: lamp lit + Accent equaliser glyph instead of the count.
- Album wall: rounded tiles (radius 20), 2 columns, title under each in Body 14, max 1 line.
- Search: keyboard stays shared; the field is a Panel pill with a search glyph; results as pill rows.
- A–Z jump (`azjump.c`): big Caps letters on a Panel ring, current letter in Accent.

### 4.5 Settings (`SCR_SETTINGS`, `SCR_SETTING_DETAIL`, `SCR_DATETIME`, `SCR_COLORPICK`, `SCR_MODEINFO`, `SCR_WORKMODE`)

- Rows: Row label left, switch or value + chevron right, Rule between rows.
- Selectors (detail pages): options as pill rows with a lamp; the chosen one lit. Help text below in Body 14, never scrolling vertically inside a label.
- Date/time: two Display-size wheels (hours/minutes) with a dial-scale indicator.
- Colour picker: only for themes that use it; for Braun 2.0 offer **accent choices** (Orange, Signal yellow `#F2B705`, Green `#4E8F5A`, Blue `#2F6DB5`) as knobs with lamps.

### 4.6 Connections (`SCR_BT`, `SCR_BT_INFO`, `SCR_WIFI`, `SCR_WIFI_INFO`)

- Top: large knob (r 30) with the radio glyph; lamp + caps "ON"/"OFF" under it; tap toggles.
- Devices/networks: pill rows; connected = lamp lit + "Connected" Body in Text 2; signal as 1–4 small bars in Text.
- Info pages: label/value rows; actions (Forget, Disconnect) as pill rows at the bottom, destructive in Text with an "×" glyph — no red.

### 4.7 EQ (`SCR_EQ`, `SCR_EQ_EDITOR`), Tune (`SCR_TUNE`) — see prompt §7.2

### 4.8 Weather (`SCR_WEATHER`) — see prompt §7.1

### 4.9 Standby (`SCR_SAVER`)

- Map each saver style (Cover, Analog, Minimal, Digital, Vinyl) to a Braun 2.0 look:
  Analog = dimmed hour ticks + two hands, Accent hub; Digital = Display 72 in Text 2;
  Minimal = time + date caps only; Cover = round art Ø 200; Vinyl = Record layout without keys.
- No grille, no second hand, redraw once a minute. Dim everything to ~55% luminance.

### 4.10 Volume overlay & toasts

- Volume overlay: the amplifier knob from `braun2_03` (knob Ø 184, rim tick scale, Accent pointer, Display value) — appears on hardware volume keys, dismisses after 1.5 s.
- Toasts per §3.

### 4.11 Track & app screens
(`SCR_SONGINFO`, `SCR_LYRICS`, `SCR_NPMENU`, `SCR_NPHUB`, `SCR_STATS`, `SCR_USAGE`, `SCR_LASTFM`, `SCR_APPS`, `SCR_APPSHELF`, `SCR_APPPICK`, `SCR_APPLAND`, `SCR_SHELF`, `SCR_ROULETTE`, `SCR_ROULETTE_SPIN`, `SCR_SPHERE`, `SCR_MA`, `SCR_SCCONFIG`, `SCR_DEBUG`)

- Song info: label (Caps) / value (Row) pairs, two per screen height, scrolling list.
- Lyrics: current line Title 22 Text, others Body 16 Text 3; no background panel.
- NP menu: up to 6 pill rows (Like, Sleep timer, Shuffle, Repeat, Add to playlist, Song info) with lamps for toggles.
- Stats / Usage: bar charts as tick columns (Text) with the current period in Accent; values in Body.
- Apps: icons on knobs (r 28) in a 3-column grid, caption Micro under each.
- Roulette / Sphere: keep their mechanics; recolour to Panel/Text/Accent; the spin pointer is the Accent needle.
- Debug: shared, minimal theming (canvas + text roles).

---

## 5. Implementation sequence

Follows themes.md §5. Each step should build, flash and render before the next.

1. **Brief & baseline** — record the base revision; copy the handoff template (themes.md §8) into `docs/themes/braun2.md`.
2. **Register** — `THEME_PRESET_BRAUN2` appended in `theme.h`; name "Braun 2.0" in the names table; `FORK_BRAUN2` palettes in `theme.c` (init from `FORK_BRAUN`, apply §2.1); add to the picker order in `settings.c`. **Do not renumber presets.**
3. **Inherit** — `theme_kit_braun2` = `theme_kit_braun` with only `id` changed. `th_braun()` stays Braun-classic; add `th_braun2()` and `th_braun_family()`; switch existing Braun call sites that should apply to both to `th_braun_family()`. Render every screen: it should look like Braun with the new palette.
4. **Fonts** — generate Inter sizes needed by §2.2 that aren't already built (check `ui/fonts/`, `font_squash_inter_*`); add to `br_font()`.
5. **Components** — implement §3 in `braun2.c`; unit-render each on a test screen.
6. **Distinctive screens** — `br2_home.c` (Home B + A option), Now Playing layout via `fork_np_layout`, QS via `kit->quicksettings` / `qs_tile`, saver via `kit->saver`.
7. **Everything else** — header builder (`kit->header`) and styler (`kit->style`) so lists, settings, connections and app screens adopt the vocabulary with no per-screen code; then special-case EQ, Weather, Volume, A–Z.
8. **Theme options** — `Settings → Display → Braun 2.0 Options` (keys prefixed `braun2_`):
   | Key | Values | Default | Apply |
   |---|---|---|---|
   | `braun2_home` | Ring status / Full-face clock | Ring status | rebuild |
   | `braun2_music` | Record / Type first | Record | rebuild |
   | `braun2_qs` | Four keys / Switch list | Four keys | rebuild |
   | `braun2_grille` | On / Off | On | live |
   | `braun2_accent` | Orange / Yellow / Green / Blue | Orange | restart |
   | `braun2_numerals` | Off / On (Full-face clock) | Off | rebuild |
9. **Help** — How-to topics for rim scrubbing (Record), the volume arc, QS pages.
10. **Acceptance** — §6, then package per themes.md §7.

---

## 6. Acceptance checklist

**Visual (native 360 × 360, both variants)**
- [ ] All text inside Ø 300; nothing clipped by the bezel.
- [ ] Longest title/artist, international text (CJK, Arabic), missing artwork, empty library, no Wi-Fi/BT, low battery, charging.
- [ ] Orange appears only on on/primary/now elements.
- [ ] Every on/off state readable without colour (lamp + text/position).

**Interaction**
- [ ] Every target ≥ 44 px effective; rim scrubbing doesn't trigger edge swipes.
- [ ] All CORE parts work; REACHABLE parts have a route (fav, sleep, mode).
- [ ] QS edits reach the view unchanged; capacity agrees with the editor.
- [ ] Back routes return to the Braun 2.0 versions of screens.

**Compatibility**
- [ ] Braun classic, Disco, Null, Squash, Zune, Ring, Stone render identically to baseline.
- [ ] No saved preset ID or option index changed.

**Performance**
- [ ] Clock faces / scales drawn once to canvas; only hands/needles move.
- [ ] No per-tick redraw of artwork; progress updates ≤ 1×/s.

---

## 7. Subscreen design prompts

Copy-paste prompts for designing (or asking an agent to design) each remaining screen.
Each assumes this guide is attached.

### 7.1 Weather (`SCR_WEATHER`)

```
Design the Weather screen for the Braun 2.0 theme (360×360 round, see BRAUN2_GUIDE.md tokens and components).
Content: current temperature, condition, high/low, feels-like, humidity, wind, and an hourly forecast (next 6–12 h)
plus 3–5 day forecast. Requirements:
- Current temperature in Display 64–72 Bold, condition in Title 22, high/low as Body "H 18° · L 9°".
- Represent the hourly forecast as a rim tick scale along the lower arc: one major tick per hour, the
  temperature curve encoded as tick length, the current hour as the Accent needle; tap/drag the rim to
  read an hour (value shown in the centre).
- Daily forecast on a second page as 3–5 pill rows: day (Caps), glyph, H/L right-aligned.
- Glyphs: simple monoline weather icons (2 px stroke, Text colour); no coloured icons.
- Show "Updated 10:05" in Micro Text 3; stale (>2 h) or offline → readable message + "Retry" pill row.
- Keep all text inside the Ø300 safe circle; keep the shared weather data/refresh logic.
Deliver light and dark renders, plus the stale and loading states.
```

### 7.2 EQ (`SCR_EQ`, `SCR_EQ_EDITOR`, presets in `eqpreset.c`, custom in `eqcustom.c`)

```
Design the EQ for Braun 2.0 (360×360 round, BRAUN2_GUIDE.md).
- EQ overview: preset list as pill rows with lamps (Flat, Bass, Vocal, …, Custom); the active preset lit.
  A small read-only curve preview (Text line, Accent dot per band) in the header cap.
- EQ editor: bands as vertical faders styled after Braun mixing desks — slot (Panel 2, 6 px wide, radius 3),
  zero line (Rule), fader cap = small knob (24×16 rounded rect, Knob colour) with an Accent index line when
  focused. Frequency labels in Micro under each fader, value of the focused band in Numeric L (44 Bold)
  centred above, e.g. "+3 dB / 250 Hz".
- Fit the band count to the circle: faders shorten toward the left/right edges following the bezel;
  if more than 6 bands, page or scroll horizontally with a dial-scale page indicator.
- Disabled EQ (e.g. when the output bypasses it): faders in Text 3, banner text explains why.
- Reset and Save as pill rows; Save names a custom preset via the shared keyboard.
Reuse existing EQ roles (EQ_SLOT, EQ_FADER, EQ_ZERO, EQ_CAP_DISABLED). Render light/dark, enabled/disabled.
```

### 7.3 Volume overlay

```
Design the Braun 2.0 volume overlay: an amplifier knob (Ø ~184, Knob colour, two faint inner rims) with
a rim tick scale (31 ticks, majors every 5) from 135° to 405°, lit up to the level; Accent pointer from
45% to 82% of the knob radius; value in Display 44 Bold on the knob (On-knob colour); output name in
Caps below on the segment ("VOLUME · HEADPHONES"). Drag around the knob to set; hardware keys step.
Show the max-volume warning (if any) as a toast. Light/dark, 0, 50, 100, and muted states.
```

### 7.4 Bluetooth & Wi-Fi

```
Design Bluetooth and Wi-Fi for Braun 2.0. Top: radio knob (r 30) with lamp and "ON/OFF" caps; tap toggles.
Below: device/network pill rows (Row 19), state in Body Text 2 ("Connected", "Saved", "Pairing…"),
signal as 1–4 small Text bars, codec or band in Micro. Scanning: a slowly sweeping Accent needle on a short
dial scale under the knob instead of a spinner. Info pages: Caps label / Row value pairs; Forget/Disconnect
as pill rows. Empty/off/error states with readable guidance. Light/dark.
```

### 7.5 Library & lists

```
Apply Braun 2.0 to the Library and all list screens: header cap with Title 22, 3–4 pill rows (56 high,
pitch 62) that narrow along the circle, lamp marks focus/playing, counts in Body Text 3, chevrons for
drill-down. Album wall: 2-column rounded tiles (radius 20) with one-line titles. A–Z jump: letters in
Caps on a Panel ring, current letter Accent. Empty library, long names, CJK names. Light/dark.
```

### 7.6 Now Playing menu, Song info, Lyrics, Queue

```
Design the Now Playing hub for Braun 2.0: NP menu as up to 6 pill rows with lamps for toggles (Like,
Sleep timer, Shuffle, Repeat, Add to playlist, Song info). Song info as Caps label / Row value pairs.
Lyrics: current line Title 22 Text, surrounding lines Body 16 Text 3, no panel behind. Queue/Up next:
pill rows, the playing item with a lit lamp and Accent bars glyph; drag handles as two short Text 3 rules.
```

### 7.7 Settings & selectors

```
Apply Braun 2.0 to Settings: rows with Row label left and slide switch or value + chevron right,
Rule separators, header cap. Detail selectors: options as pill rows with lamps, help text in Body 14 below.
Date/time: two Display wheels with a dial-scale indicator. Include the Braun 2.0 Options page from §5.8.
```

### 7.8 Standby

```
Map the five saver styles (Cover, Analog, Minimal, Digital, Vinyl) to Braun 2.0 at ~55% luminance on
near-black (#0E0E0D): no grille, no second hand, once-a-minute redraw, Accent only on the clock hub.
Track line in Body 14 Text 3 at the bottom of the safe circle. Show all five.
```

### 7.9 Stats, Usage, Battery

```
Design Stats/Usage for Braun 2.0: bar charts as columns of short ticks (Text), the current period in
Accent, axis labels in Micro, totals in Title. Battery/charge history as a rim arc plus a tick-column
chart; charging bands in Panel 2. Keep all text inside the safe circle. Light/dark.
```

### 7.10 Apps, Shelf, Roulette, Sphere

```
Apply Braun 2.0 to the app launcher (icons on r 28 knobs, 3-column grid, Micro captions), the shelf,
Roulette (spin pointer = Accent needle, segments in Panel/Panel 2 alternating, result in Title) and Sphere
(recolour to Text/Text 3 with the selection in Accent). Mechanics unchanged.
```

---

## 8. Handoff template

```markdown
# Braun 2.0
- ID / baseline: braun2 / <commit>
- Visual brief and native-size references: BRAUN2_GUIDE.md, renders braun2_01–09
- Palette / font sources / licences / fallback chain: §2.1 / Inter (OFL) / shared intl chain
- Supported appearance and handedness: light + dark; right-hand only (no mirrored layouts) — or state otherwise
- Shared features inherited:
- Custom views and their shared action/data owners:
- Intentional restrictions and disabled explanations: (see themes.md §8 for the remaining fields)
```
