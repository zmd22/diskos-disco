# Settings

All themes share seven root categories. Rows depend on theme and firmware: inapplicable options are hidden; options blocked by a dependency stay visible with an explanation. Menu sorting never changes saved values.

| Category | Contents |
|---|---|
| Playback | Play Mode, Equalizer, Library seek, Resume Playback, Artists grouping, Auto-tag, Play Through Folders, Up Next, SongSphere. |
| Audio | Working Mode, Gain, DAC Filter, ReplayGain, DRE, Gapless, Max Volume, Balance, SPDIF Output (where supported), DSD Output (read-only). |
| Display | Brightness, Theme, <theme> settings, Light / dark, Immersive, Library appearance, Navigation, QS style, Text & layout. |
| Connectivity | Wi-Fi, Bluetooth (devices, codec), aptX, Quiet Wi-Fi for Bluetooth, MA Sendspin, Online services. See [Bluetooth](BLUETOOTH.md). |
| System | Language, Controls, Date & Time, Library management, Maintenance. |
| Battery & power | Details, Idle power-off, Lock on timeout, Restart, Screen-off delay, Screensaver delay, Shut down player, Sleep timer, battery percentage, Charging limit. |
| Help | How to, About, Interaction hints, Show hints again. |

## Display

- **Theme** selects Disco, Ring, Braun, Braun 2.0, Stone, Null, Squash, Zune or Graphite. 
- **<theme> settings** holds Appearance, Music screen, Navigation, Quick settings layout and Screensaver design, where applicable.
- **Light / dark** Dark, Light or Auto (day = 07:00–19:00, switches while the screen is off). Null is dark only.
- **Immersive** Style (Vinyl/CD/Cover), lyrics, Circular control (Duration/Volume).
- **Library appearance** List/Wall/Coverflow, album track numbers.
- **Navigation** shared Quick settings items, Shortcuts, Back-swipe distance. Theme menus and gestures are under the theme's own Navigation page.
- **Text & layout** text size, rotation, transitions, Progressive lyrics and Estimate word timing (word highlights in Lyrics and Immersive). Music-title font is set separately.
- **QS style** Classic or Shade. Shade's first pull shows now playing (except on Music); a second pull or the handle opens Quick Settings.

**List motion** (**Display › <theme> settings › Navigation**; Disco, Null, Squash): Classic, Straight, Disco, Wheel. Wheel scales and tints rows around the centre row while keeping headers and action rows (e.g. an album's Play / Shuffle) in place. Main library lists show their item count above the first row.

## Theme differences

| Theme | Specific options |
|---|---|
| Disco | Artwork-backed Music, Buttons/Auto-hide/Gestures, text alignment/font, progress shape/colour, rim sheen, right/left navigation and List/Arc Quick Settings layout. |
| Null | Fixed-dark palettes (Cyan/Yellow, Red/Lime, Matrix), its own Music background/font/alignment and menu controls, right-side navigation, Text/Icons Quick Settings layout. No custom accent selector. |
| Squash | Plain/Blur/Cover background, independent Music font preference, playback state/mode indicators, gesture Music controls and fixed right-side navigation/QS layout. No custom accent selector. |
| Zune | Dark/Light/Auto, accent and main-icon/menu assignments, lock-screen content and clock tint. No separate Now Playing style selector. |
| Braun 2.0 | Dark/Light/Auto, fixed orange accent, Music Controls (Buttons/Gestures) and Layout (Art left/Art on top), independent eight-item Quick Settings selection, and shared saver choices. |
| Graphite | Dark/Light/Auto with Outdoor right under it, Backlight (White/Grey/Amber, Light only), Spectrum bars on Now Playing (Off shows the track's format), fixed ink accent, its own Quick Settings selection. |
| Ring / Braun / Stone | Own appearance, Music and saver controls. The **Ring Now Playing layout** is unrelated to the **Ring theme**. |

## Other paths

- **Connectivity › Online services**: online artwork, online lyrics, artwork cache and weather.
- **System › Date & Time**: date/time and 24-hour format.
- **Battery & power**: delays (screen-off delay adds to the screensaver delay). Saver design is under **<theme> settings › Screensaver**.
- **Battery & power › Lock on timeout** (Off by default): On, a screen that turns off by itself locks like the power button: touches can't wake it, one press does. Off, a tap wakes it.
- **Battery & power › Sleep timer**: Duration and When sleep ends (separate from idle power-off).
- **Help › About**: version and build ID.
- **System › Maintenance**: SD update, reset, Debug Mode.

DSD Output is informational (Player-managed), not a Native/DoP/PCM switch. Gain, codec, power, rotation and key controls are firmware-guarded; their presence doesn't imply support on other firmware.

Source: `ui/settings.c`, `ui/settings_display.h`, `ui/settings_order.h`, `ui/theme.c` and the feature handlers.
