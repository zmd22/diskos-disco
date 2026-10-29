# Settings in Disco 3.2.1

The seven root categories are shared by all themes. Visible rows depend on theme and firmware capability. Most irrelevant theme options are hidden; dependency-blocked options can remain visible with an explanation. Saved option values are not reordered when the menus are sorted.

| Category | Current contents |
|---|---|
| Playback | Play Mode, Equalizer, Library seek, Resume Playback, Artists grouping, Auto-tag, Play Through Folders, Up Next and SongSphere. Play Mode and Equalizer lead; SongSphere is last. |
| Audio | Working Mode, Gain, DAC Filter, ReplayGain, DRE, Gapless, Max Volume, Balance, supported SPDIF Output and read-only DSD Output. |
| Display | Brightness, Theme, selected theme settings, supported Light / dark, Immersive, Library appearance, Navigation, QS style and Text & layout. |
| Connectivity | Wi-Fi, Bluetooth (Devices and codec), MA Sendspin and Online services. |
| System | Language, Controls, Date & Time, Library management and Maintenance. |
| Battery & power | Details, Idle power-off, Restart, Screen-off delay, Screensaver delay, Shut down player, Sleep timer, supported battery-percentage display and Charging limit. |
| Help | How to, About, supported Interaction hints and Show hints again. |

## Display

- **Theme** selects Disco, Ring, Braun, Stone, Null, Squash or Zune. Retired preset IDs are not offered.
- **<theme> settings** owns applicable Appearance, Music screen, Navigation, Quick settings layout and Screensaver design. Empty or irrelevant pages are omitted.
- **Light / dark** offers Dark, Light and Auto for supported themes, including Zune. Null is fixed-dark. Auto changes at an off-screen boundary; day is 07:00–19:00.
- **Immersive** owns Style (Vinyl/CD/Cover), lyrics and optional Circular control with Duration/Volume action.
- **Library appearance** owns List/Wall/Coverflow and album-list track numbers.
- **Navigation** owns shared Quick settings items, Shortcuts and Back-swipe distance. Theme menu destinations and gestures live in the theme's Navigation page.
- **Text & layout** contains shared text sizing, screen rotation and transitions where supported. Music-title font selection is separate.
- **QS style** selects Classic or Shade. Off Music, Shade opens a now-playing panel first; a second pull or the handle opens Quick Settings. Music opens Quick Settings directly.

List motion is under **Display > <theme> settings > Navigation**. Disco, Null and Squash offer Classic, Straight, Disco and Wheel. In Wheel, rows scale and change colour continuously around the central row; the 3.2.1 layout respects list headers and action controls. Library main lists show their count above the first row.

## Theme differences

| Theme | Applicable distinctions |
|---|---|
| Disco | Artwork-backed Music, Buttons/Auto-hide/Gestures, text alignment/font, progress shape/colour, rim sheen, right/left navigation and List/Arc Quick Settings layout. |
| Null | Fixed-dark palettes (Cyan/Yellow, Red/Lime, Matrix), its own Music background/font/alignment and menu controls, right-side navigation, Text/Icons Quick Settings layout. No custom accent selector. |
| Squash | Plain/Blur/Cover background, independent Music font preference, playback state/mode indicators, gesture Music controls and fixed right-side navigation/QS layout. No custom accent selector. |
| Zune | Dark/Light/Auto, accent and main-icon/menu assignments, lock-screen content and clock tint. No separate Now Playing style selector. |
| Ring / Braun / Stone | Their own applicable appearance, Music and saver controls. The **Ring Now Playing layout** is distinct from the **Ring theme**. |

## Other important paths

- **Connectivity > Online services**: online artwork, online lyrics, artwork cache and weather.
- **System > Date & Time**: date/time and 24-hour format.
- **Battery & power**: activation delays; **theme settings > Screensaver**: supported saver design. Screen-off delay is additional to screensaver delay.
- **Battery & power > Sleep timer**: Duration and When sleep ends. This explicit timer is distinct from idle shutdown.
- **Help > About**: running version and binary-derived build ID.
- **System > Maintenance**: signed SD update, reset and debug entry points.

DSD Output is information labelled Player-managed, not a Native/DoP/PCM selector. Gain, codec, power, rotation and key controls have firmware/install guards. A setting's presence does not prove support on another stock firmware.

Source: `ui/settings.c`, `ui/settings_display.h`, `ui/settings_order.h`, `ui/theme.c` and the feature handlers.
