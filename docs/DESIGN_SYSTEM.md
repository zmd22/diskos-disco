# diskOS design system

This is the visual and interaction baseline for the device UI, boot experience, project imagery,
and supporting pages. It is intentionally small: diskOS should feel calm, precise, premium, and
native to a round 360 x 360 display.

<p align="center">
  <img src="assets/boot-animation.gif" alt="diskOS boot animation concept" width="360">
  <br><sub>Boot animation concept: progress moves clockwise from the lower-left.</sub>
</p>

## Product principles

1. **Round-native:** compose from the center outward and treat the circular edge as useful space.
2. **Music first:** artwork, track identity, and primary playback actions get the strongest contrast.
3. **Calm by default:** use one accent family at a time, limited glow, and purposeful motion.
4. **Readable at a glance:** short labels, large targets, and clear hierarchy beat dense controls.
5. **Honest motion:** progress always moves forward. Indeterminate work uses a moving segment rather
   than a fake percentage.

## Core tokens

### Color

| Token | Value | Use |
|---|---:|---|
| `ink-950` | `#07080A` | Screen background |
| `surface-900` | `#111318` | Cards and sheets |
| `surface-800` | `#1B1E24` | Pressed or elevated surfaces |
| `ring-track` | `#2A2830` | Inactive ring and dividers |
| `text-primary` | `#F6F2F8` | Titles and essential values |
| `text-secondary` | `#AAA6B0` | Metadata and supporting labels |
| `accent-steel` | `#C9CDD2` | Default progress and selection |
| `accent-warm` | `#A9A39A` | Secondary neutral detail |
| `success` | `#84C49B` | Completed and connected |
| `warning` | `#E9B36A` | Recoverable warnings |
| `danger` | `#EB706C` | Destructive or failed state |
| `focus` | `#DCC4EA` | Focus and leading playhead dot |

Album-derived accents may replace `accent-steel`, but keep lightness high enough to remain visible
against `ink-950`, cap saturation to avoid neon glare, and preserve white for the leading dot.
The boot signature stays monochrome, moving from brushed steel to warm white. Color belongs to album
art and listening screens, not startup chrome.

### Type

- UI family (as shipped): **Montserrat** for Latin text, with **Source Han Sans** as the CJK fallback. **Inter** is shipped in the hi-fi theme; other themes have their own bundled fonts.
- Display: 28 px / 32 px, semibold.
- Title: 20 px / 24 px, semibold.
- Body: 16 px / 21 px, regular.
- Metadata: 13 px / 17 px, medium.
- Avoid more than two weights on one screen. Do not place essential text in the curved edge band.

### Space and shape

- Base spacing unit: **4 px**. Use 8, 12, 16, 24, and 32 px steps.
- Minimum touch target: **44 x 44 px**.
- Central safe zone for essential controls: **280 px diameter**.
- Corners: 12 px compact, 18 px card, 999 px pill.
- Borders: 1 px standard; 2 px for selected or focused controls.

## Canonical edge ring

The ring is a 270 degrees path near the outer display edge, with a 90 degrees gap centered at the bottom.

| Property | 360 x 360 value |
|---|---:|
| Center | `180, 180` |
| Radius | `172 px` |
| Stroke | `3 px` |
| Start | `135 degrees`: lower-left, about 7:30 |
| End | `405 degrees`: lower-right, about 4:30 |
| Direction | Clockwise |
| Leading dot | `7.2 px` diameter, `focus` color |

For playback or determinate loading, the filled path starts at 135 degrees and grows clockwise. The dot
sits on the leading edge. Never mirror the path, start at the top, or fill the inactive section.

Use the ring for one continuous value only: playback position, volume during adjustment, or boot
progress. Direct seeking is allowed only on the Now Playing screen. Other screens keep the edge free
for bezel scrolling.

## Motion

| Token | Duration | Use |
|---|---:|---|
| `motion-instant` | 80 ms | Press feedback |
| `motion-fast` | 160 ms | Small state change |
| `motion-standard` | 240 ms | Screen or component transition |
| `motion-emphasis` | 420 ms | Artwork or large reveal |
| `motion-boot-demo` | 2400 ms | Full boot concept preview |

Use `cubic-bezier(.22, .8, .24, 1)` for entry and progress settlement. Avoid perpetual pulsing. With
reduced motion enabled, show the wordmark immediately, use a static 35% ring for indeterminate startup,
and crossfade to Home when ready.

## Boot screen and boot animation

### Sequence

1. **Wake, 0-220 ms:** `ink-950` fades in; the inactive edge path becomes visible.
2. **Resolve, 220-650 ms:** the `diskOS` wordmark rises into focus while one fine signal highlight
   sweeps from left to right.
3. **Load, until ready:** the ring advances clockwise from the lower-left, driven by real boot stages.
4. **Ready, 280 ms:** complete the remaining arc, hold for 100 ms, then crossfade to Home.

The 2.4-second GIF is a visual timing sample. Production should bind progress to boot stages and
must not reach 100% before the home screen is ready. If real progress is unavailable, animate a
72 degrees segment clockwise along the same path and never show a percentage.

### Boot states

| State | Ring | Center | Behavior |
|---|---|---|---|
| Starting | Track + early progress | Wordmark resolving | No text beyond `diskOS` |
| Loading | Forward progress | Stable wordmark | No bounce or reverse motion |
| Ready | Full 270 degrees path | Mark at full contrast | Short hold, then crossfade |
| Recoverable delay | 72 degrees moving segment | Muted wordmark | Keep motion slow and steady |
| Error | `danger` at failed position | Small error code | Stop motion; preserve the code |

## Component rules

### Buttons

- Primary: accent fill, dark label, 44 px minimum height.
- Secondary: `surface-800`, `text-primary`, 1 px border.
- Ghost: transparent, used only where hierarchy is already obvious.
- Pressed: reduce brightness by 8% and scale to 98% for 80 ms.

### Lists

- Prefer five or fewer visible rows at once.
- Use 56 px compact rows and 68 px artwork rows.
- Keep primary labels left-aligned inside the safe zone; reserve the outer edge for scrolling.
- Truncate long text once. A marquee may begin only after a 700 ms pause.

### Artwork

- Album art may be circular or softly rounded according to context.
- Use blur only as atmosphere behind readable content, never as the content surface itself.
- Extract one accent family from artwork; retain neutral text and controls.

### Feedback

- Success is quiet: icon or short line of text, dismissed automatically.
- Warnings explain the next safe action.
- Errors keep stable codes for support and never disappear without user action.

## Accessibility and quality checks

- Maintain at least 4.5:1 contrast for body text and 3:1 for large text and controls.
- Do not encode status through color alone; pair it with an icon, label, or stable position.
- Test every screen at native 360 x 360 resolution, not only in an enlarged mockup.
- Check the 44 px target minimum with fingers, not a pointer.
- Confirm the ring begins at lower-left, moves clockwise, and remains inside the display edge.
- Keep motion readable at 30 fps and provide a reduced-motion state.

## Reference assets

- [`assets/boot-animation.gif`](assets/boot-animation.gif): one-shot preview
- [`assets/boot-screen.png`](assets/boot-screen.png): native 360 x 360 reference frame
- [`assets/boot-storyboard.png`](assets/boot-storyboard.png): six-frame handoff
