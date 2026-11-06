# Immersive 3.0

Disco, Null, Squash and Zune share the spinning-disc transition and transport overlay. Tap the artwork to show title,
artist, time and Previous / Play-Pause / Next; it hides when idle. Leave with the left-edge Back swipe. Animation
follows the global setting.

## Styles

Vinyl, CD and Cover (plain artwork; replaces "None", same saved value) all rotate, with optional lyrics. New lyric
lines slide up over 240 ms; text is rendered once and reused, not reallocated per frame.

## Circular control

**Display › Immersive › Circular control** (default Off), then **Control action: Duration / Volume**. Rotate the
artwork clockwise to increase, counterclockwise to decrease. The image tracks your finger (also when paused) and
resumes from the release angle; crossing the centre re-anchors without a jump.

- **Duration:** a box shows target time / total. Lyrics pause while dragging; release performs one seek. A track
  change or lost touch cancels it. Stale position updates are ignored briefly after the seek.
- **Volume:** shown in the themed volume overlay, written at most every 120 ms plus a final value, capped at the
  configured maximum. Lyrics continue. The overlay ignores touches so dragging isn't interrupted.

With Circular control on, tapping the cover doesn't exit; use the Back swipe (there's no exit arrow).

## Null

Null's regular Music screen also supports hold-and-drag seeking with a time preview and a single seek on release.

Tested via native builds, LVGL renders and focused checks; extended on-device gesture and battery testing is still
pending. No battery-life gain is claimed.
