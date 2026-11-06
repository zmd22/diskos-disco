# Settings and submenu template

The settings hierarchy for all themes. Styling may differ per theme; hierarchy, row order, action targets and saved
values must not. User-facing reference: [DISPLAY_SETTINGS.md](DISPLAY_SETTINGS.md).

## Root categories

1. Playback
2. Audio
3. Display
4. Connectivity
5. System
6. Battery & power
7. Help

`GROUPS` in `ui/settings.c` is authoritative; lists, circular menus and the Zune dial all use it. Every category,
including Help, must stay reachable.

## Submenu ordering

All lists (Zune included) sort via `settings_collect()` / `setting_compare()` in `ui/settings_order.h`. No
theme-specific sorters.

1. Pinned-first entries.
2. Actions, info, sliders and multi-option rows, alphabetical by English label (case-insensitive).
3. On/Off rows (toggles, and selectors whose options are exactly Off/On), alphabetical.
4. Pinned-last entry.

Playback pins Play Mode, then Equalizer, first and SongSphere last. Resume Playback (Off/Position/Song) isn't On/Off.

Sorting applies to rows only: never reorder preset IDs, stored indices, ordered actions (theme choices) or
user-ordered Shortcuts/QS editors. Keys, defaults and handlers survive moves. Library seek stays 0 = A-Z keys,
1 = A-Z wheel.

## Row presentation and routing

- Actions open the same screen in every theme. **Playback › Equalizer** opens `SCR_EQ`; don't bring back the old
  `eq_preset` cycler or a second Custom EQ row.
- Theme settings use the shared presentation/visibility map. Unavailable rows keep their explanation; disabled rows
  never change values.
- Info and unavailable values go under the title when two columns would clip. Keep the theme's fonts, colours, row style.
- Circular root captions may wrap.
- Subpage Back goes to its `SUBGROUP` parent; category Back goes to Settings (Zune too; don't flatten System for Zune).
- Keep `display_group_alias()` so old Network/Power deep links reach Connectivity / Battery & power.

## DSD information

**Audio › DSD Output** is read-only ("Player-managed"). The old Auto value was a placeholder with no key, callback or
command. There's no verified DSD mode control; stock-player capability doesn't prove a Native/DoP/PCM switch would
work. Don't invent a command, write a sysconfig DSD column or present this row as a selector.

## Verification before merging

```sh
make -C ui CROSS= host-render -j2
python3 ui/tests/settings_alignment_test.py
```

Check every theme in light and dark: row order, one Equalizer action, read-only DSD, Battery navigation, Back paths,
preserved preferences. Render root and changed submenus at 360×360 and inspect clipping, caption spacing, disabled
info and scrolling. This doesn't replace on-device audio testing or a target build.

## Approved hierarchy

| Category | Ordered contents |
| --- | --- |
| Playback | Play mode; Equalizer; alphabetical choices; alphabetical On/Off items; SongSphere last |
| Audio | Balance; DAC filter; DSD output (read-only); Gain; Max volume; ReplayGain; Working mode; DRE; Gapless; supported SPDIF |
| Display | Brightness; Theme picker; selected theme settings; supported Light / dark; QS style; Immersive; Library appearance; Navigation; Text & layout |
| Connectivity | Wi-Fi; Bluetooth (Devices, codec); aptX; Quiet Wi-Fi for Bluetooth; MA Sendspin; Online services |
| System | Language; Controls; Date & time; Library management; Maintenance |
| Battery & power | Details; Idle power-off; Restart; Screen-off delay; Screensaver delay; Shut down player; Sleep timer; supported battery-percentage display; Charging limit |
| Help | How to; About; supported interaction hints; Show hints again |

Theme settings hold Appearance, Music screen, Navigation, Quick settings layout and Screensaver pages where they
apply; empty submenus are omitted. List motion and Zune icon/menu assignments are theme-owned. QS items and Shortcuts
are shared Navigation entries with per-theme stored choices.

Sleep timer: Duration and When sleep ends. Screen-off and screensaver timing are in Battery & power; saver design is
theme-owned. Startup disc colour and the preset-only Equalizer shortcut were removed.
