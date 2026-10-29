# Settings and submenu template

Current hierarchy in Disco 3.2.1. Display ownership is documented in
[DISPLAY_SETTINGS_SPLIT.md](DISPLAY_SETTINGS_SPLIT.md). Explicit anchors take
precedence over alphabetical ordering.

Use this structure for all themes. Theme styling can change, but the category
hierarchy, row ordering, action destinations and saved-value meanings must agree.

## Root categories

1. Playback
2. Audio
3. Display
4. Connectivity
5. System
6. Battery & power
7. Help

`GROUPS` in `ui/settings.c` is authoritative. Lists, circular menus and the Zune
dial use that same array. Keep every category reachable, including Help.

## Submenu ordering

Use `settings_collect()` and `setting_compare()` in `ui/settings_order.h` for
both normal and Zune settings lists. Do not create a theme-specific sorter.

1. Explicitly requested pinned entries come first.
2. Actions, information, sliders and multi-option settings follow in alphabetical
   order, comparing their canonical English display labels without case sensitivity.
3. On/Off settings follow those rows, also alphabetically. This includes genuine
   toggles and two-choice selectors whose options are exactly Off and On.
4. An explicitly requested last entry follows both bands.

Playback pins Play Mode first and Equalizer second, and SongSphere last.
Resume Playback is not an On/Off toggle: its options are Off, Position and Song.

This rule sorts menu rows only. Never alphabetise preset IDs, stored option
indices, ordered actions (such as theme choices), or user-reordered shortcut and
Quick Settings editors. Existing saved keys, defaults and handlers survive moves.
Library seek retains 0 = A-Z keys and 1 = A-Z wheel.

## Row presentation and routing

- Actions open the same screen regardless of theme. Playback > Equalizer opens
  SCR_EQ, the existing profile/toggle/edit/rename interface. Do not restore the
  old eq_preset settings cycler or add a duplicate Custom EQ row.
- Theme-specific settings use the shared presentation/visibility map. Unavailable
  rows retain their explanatory guard; a disabled row must not change a setting.
- Information and unavailable values appear beneath the title where two narrow
  columns would clip them. Keep the theme's existing fonts/colours and row style.
- Circular root captions may wrap to fit the 360-pixel screen.
- Subpage Back returns to its declared parent in SUBGROUP. Root category Back
  returns to Settings, including in Zune; do not flatten System only for Zune.
- Keep legacy category aliases in display_group_alias() so internal deep links
  using Network or Power reach Connectivity or Battery & power.

## DSD information

Audio > DSD Output is read-only and says Player-managed. The former Auto value
was a static placeholder, with no configuration key, apply callback or command.
No verified Disco DSD mode selector exists. Stock-player format capability is
not evidence that a Disco Native/DoP/PCM setting works. Do not invent a command,
write a sysconfig DSD column, or label this row as an active mode selector.

## Verification before merging

Build `make -C ui CROSS= host-render -j2`, then run
`python3 ui/tests/settings_alignment_test.py`. Verify all seven active themes,
light/dark appearances, row order, a single Equalizer action, read-only DSD,
Battery navigation, parent Back paths and preserved stored preferences.
Render root and affected submenus at 360 by 360 and inspect clipping, circular
caption spacing, disabled information and scrolling. These checks do not replace
physical DSD/audio testing or a target firmware build.

## Approved hierarchy

| Category | Ordered contents |
| --- | --- |
| Playback | Play mode; Equalizer; alphabetical choices; alphabetical On/Off items; SongSphere last |
| Audio | Balance; DAC filter; DSD output (read-only); Gain; Max volume; ReplayGain; Working mode; DRE; Gapless; supported SPDIF |
| Display | Brightness; Theme picker; selected theme settings; supported Light / dark; QS style; Immersive; Library appearance; Navigation; Text & layout |
| Connectivity | Wi-Fi; Bluetooth (Devices, codec); MA Sendspin; Online services |
| System | Language; Controls; Date & time; Library management; Maintenance |
| Battery & power | Details; Idle power-off; Restart; Screen-off delay; Screensaver delay; Shut down player; Sleep timer; supported battery-percentage display; Charging limit |
| Help | How to; About; supported interaction hints; Show hints again |

Theme settings contain applicable Appearance, Music screen, Navigation, Quick
settings layout and Screensaver pages. Fixed or unsupported presentation controls
are omitted. No empty theme submenu is offered. List motion and Zune icon/menu
assignments are theme-owned. Quick settings item selection and Shortcuts remain
shared Navigation entries; their stored theme choices stay independent.

Sleep timer contains Duration and When sleep ends. Screen-off and screensaver
activation timing lives in Battery & power; screensaver design remains theme-owned.
Startup disc colour and the old preset-only Equalizer shortcut are removed.

These are verification instructions, not results from this documentation pass. Host tests cannot establish physical-device playback or charging behaviour.
