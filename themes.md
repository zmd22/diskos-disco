# Theme authoring framework

A practical guide to adding a complete theme to diskOS Disco, based on the
Disco → Null → Squash implementation and the regressions found during that work.

**Rule: share behaviour, override presentation, isolate genuine theme options.**
A new theme should inherit working features before it gets its own look.
Existing themes retain their approved look and behaviour throughout.

This document covers the implemented interfaces. It describes the compiled theme/kit interfaces in 3.2.2. A general registry/capability refactor is not implemented.

## Compatibility contract: existing themes are protected

**Adding a new theme is not permission to redesign or change an existing one.**
Use the current approved implementation as the baseline for each existing
preset, including presets retained in code but hidden from the picker.

Preserve all of the following unless the owner explicitly requests a change:

- Look: colours, font faces/sizes/fallbacks, spacing, geometry, artwork treatment,
  transitions, timing and feedback.
- Function: every implemented action, gesture, hold/double-tap, back route,
  popup, editor, transport operation and state indication.
- Preferences: saved keys/IDs, defaults, available choices, scope, persistence,
  disabled states and each theme's own configuration.
- System behaviour: playback, radio commands, artwork/cache work, app handoff,
  wake/standby and resource lifetime.

A new theme may use its own layout, controls and exclusive features, provided
it preserves the shared core actions or an explicitly approved restriction.
Those differences belong to that theme's views and options, not to changes in
the old themes' defaults or styling.

### Rules for shared-code changes

1. Extract existing behaviour without changing its observable results. Keep
   compatibility adapters where the old themes need them.
2. Give new theme-only behaviour an explicit opt-in. Do not activate it for old
   themes because they inherit a new global default.
3. When replacing theme-name branches with capabilities, first record complete
   profiles for the old themes. A missing capability entry must not silently
   remove an existing feature or route.
4. Do not change a shared palette/font role, base component or layout constant
   just to make the new theme look right. Override it in the new theme.
5. Keep an existing theme's settings section and menu order intact unless a
   change to that theme has been explicitly requested.
6. If an old-theme bug is discovered, record it separately. Fixing it is a
   separate scoped change, not an undocumented side effect of adding a theme.
7. Compare affected old-theme views and interactions against their baseline.
   Freeze input data, font settings, time and animation phase for render checks;
   compare behaviour separately. A feature inventory alone cannot prove the
   appearance stayed the same.

If a shared extraction cannot preserve old results, keep the old path working
and let the new theme opt into the new path. Do not remove the old path to make
registration simpler. Any unavoidable migration needs a concrete explanation
and explicit authorization before changing the old theme.

The intended outcome is **one more theme**, with existing themes continuing to
look and work as before. This document does not authorize runtime changes.

## 1. What a theme is

A theme has three layers:

| Layer | Owns | Examples |
| --- | --- | --- |
| Shared application | Data, actions, persistence and navigation semantics | Playback, library, radio state, QS catalogue/editor, hint learning |
| Theme presentation | Layout, typography, colour, artwork treatment and motion | Null drawer, Disco arcs, pixel cover, headers and volume appearance |
| Theme options | Choices that make sense for that presentation | Null palette/background/fonts; Disco progress shape and rim sheen |

Do not copy playback, Bluetooth, Wi-Fi, queue, artwork decoding or config-writing
logic into a theme. A view can render the same shared action differently.

A JSON palette is not a complete screen theme. The current system also needs
compiled fonts, a theme definition, component hooks and, for substantially
custom layouts, C builders. Inspect the current custom-theme loader before
promising that an exported JSON file can install a new interaction/layout.

## 2. Current implementation map

These are the existing extension points, not a new API:

| Source | Responsibility |
| --- | --- |
| [ui/theme.h](ui/theme.h) | Colour/font roles, preset identifiers, traits, `theme_def_t`, public theme accessors |
| [ui/theme.c](ui/theme.c) | Palette selection, custom-theme loading, font fallback installation and active theme state |
| [ui/theme_defs.c](ui/theme_defs.c) | Named definitions: fonts, traits, texture, title case and component kit |
| [ui/theme_kit.h](ui/theme_kit.h) | Component hooks, part structures, action names and action tiers |
| [ui/theme_kit.c](ui/theme_kit.c) | Kit resolution, widget classification, styling passes and action registration |
| [ui/theme_kits.c](ui/theme_kits.c) | Existing component styles and layout overrides |
| [ui/null_theme.c](ui/null_theme.c) | Null's custom Music, navigation, QS, saver and volume presentation |
| [ui/disco.c](ui/disco.c), [ui/disco_home.c](ui/disco_home.c) | Disco navigation/catalogue, shared editors, Music and track popup |
| [ui/quicksettings.c](ui/quicksettings.c) | Shared QS catalogue, selected items, persistence and actions |
| [ui/settings.c](ui/settings.c) | Settings rows, detail controls, theme visibility and Help/How to |
| [ui/onboarding.c](ui/onboarding.c) | Hint eligibility, learning counters, context and popup presentation |
| [ui/screenmgr.c](ui/screenmgr.c) | Screen lifetime, routes, back handling and transitions |
| [ui/tests/host_render.c](ui/tests/host_render.c) | Native renders and focused interaction checks |

### Colour and font rules

- Use `TC(role)` and `TF(role)` in views. Add a semantic role if a shared component
  needs a new meaning; do not scatter hard-coded palette values through screens.
- Use `theme_color_from_rgb()` for runtime colours such as artwork-derived RGB.
  Keep explicit fixed media colours behind the existing role API.
- Define palettes in `ui/tools/gen_theme_presets.py`; regenerate
  `ui/theme_presets.inc`. Do not hand-edit generated output as the source of truth.
- Register font sources, generated faces and licences. The existing general font
  generator is `ui/tools/gen_theme_fonts.py`; Null also has its own font assets.
- Preserve symbol and international/CJK fallback chains. A display font that
  lacks weather, transport, degree or punctuation glyphs needs a fallback or an
  intentional supported replacement. Check actual glyphs, not just font names.
- Font size means the rendered face and line height, not merely the argument to
  an accessor: some accessors choose the nearest supported size.

### Builder and styling rules

`theme_kit_t` supports headers, a widget styler, Home builders/layouts, Now
Playing layouts, QS hooks, saver hooks and state/title callbacks. A missing hook
falls back to the default kit implementation. Prefer the smallest override:

1. Palette/font definition for a visual recolour.
2. A component styler for common widget treatment.
3. A layout hook over existing parts when positions change.
4. A dedicated screen builder only when the design truly needs it.

Call shared action handlers from custom builders. Use `kit_keep()` only on
widgets you deliberately style yourself; it does not automatically protect their
children. Avoid blanket protection that prevents later shared improvements.
Do not repurpose another component's LVGL flags as private state; Null's QS/list
work showed how easily flags can interfere with theme styling.

### Interaction contracts

Register actions with `ui_on()` and stable action names where possible:

- `UI_CORE`: preserve the action on its screen.
- `UI_REACHABLE`: preserve an explicit, discoverable route somewhere.
- `UI_DECORATIVE`: may be omitted without removing functionality.

For example, a theme can replace a play button with a gesture, but it still
needs a usable play/pause path and feedback. Action registration helps checks
find controls; it does not prove hit targets, long-press suppression or gesture
priority. Those need focused interaction checks.

## 3. The completeness contract

Create a theme checklist before drawing screens. Mark every row **inherited**,
**restyled**, **custom view** or **unavailable with reason**. An unreviewed row is
unfinished. A deliberately different design is not a missing feature.

| Area | Behaviour that must carry over | Theme can change |
| --- | --- | --- |
| Music | Metadata, transport, mode, seek, progress/time, track actions and configured title hold | Fonts, alignment, artwork, control geometry |
| Immersive | Explicit entry/return, progress, lyrics and correct return theme | Record treatment and transition choreography |
| Navigation | Destinations, installed app identity, editing, back and configured shortcuts | Drawer, shard, handle or another presentation |
| QS | Catalogue, saved order, add/remove, radio tap/hold, live state, brightness, transport | Text/icons, arrangement, bars/arcs, capacity |
| Library | Songs/albums/artists/playlists, actions, A-Z, focused text behaviour and restored position | Rows, cover layout, position indicator |
| Settings | Shared options, clear disabled reasons, live values and persistence | Typography and control styling |
| Help | How to, applicable gestures, first-use hints and reset | Type, pagination and visual feedback |
| System views | BT, Wi-Fi, search, track info, battery, EQ, volume, dialogs and keyboards | Visual treatment with intact actions |
| Standby | Clock, weather/available status, track info, wake and correct background | Layout, sizing and artwork treatment |
| App boundaries | Core landing/launcher readability and theme handoff | Host chrome; custom app internals only if explicitly in scope |

Particular carryovers from Disco that Null initially needed explicitly were the
clock/status visibility hold, track popup and mood pill, scroll movement option,
nav shortcuts, contextual hints, charging feedback and weather glyphs.
Treat this as a regression checklist, not a claim that every theme supports
every presentation-specific feature.

## 4. Lessons from Null: regressions to prevent

### QS edits must reach the view unchanged

The shared editor saved Null's choices, but the custom panel forced Wi-Fi and
Settings into fixed positions and refilled deleted slots. Saving appeared broken.

The corrected contract is:

- Load the shared selection, preserve its order and render exactly that list.
- Hide unused slots. Do not silently fill them or reinsert deleted items.
- The renderer and editor must agree on capacity: currently Disco seven and
  Null eight. Do not infer capacity from an older `qs_parts_t` limit.
- Changing Text/Icons changes presentation, not selected item identity.
- Editor close rebuilds the view; reopen uses the saved selection.
- Radio holds open connections; other item holds retain the shared name hint.

If a future theme truly requires pinned items, declare that policy explicitly
and enforce it in both editor and renderer. Explain the restriction in the UI.
Never make a renderer secretly contradict a successful editor save.

### Return routes must use the active theme

Music and the shared Now Playing/Immersive screen are not interchangeable.
Exercise return from Immersive, starting a track from the library, popup Back,
wake and screen recreation. A correct initial render can still return to a
partly styled Ring screen if only one entry path is themed.

### Shared Help, separate preferences

Disco and Null now use `Settings → Help` below Battery:

1. How to.
2. First-use hints.
3. Show hints again.

The menu is shared, while hint preferences and learning counters remain
separate. How to returns to Help; Help returns to Settings. Reuse the current
onboarding engine and derive descriptions from active controls/handedness.
Do not duplicate Help inside each theme-options list.

### Scope settings to what they actually affect

Null's text alignment applies to Music metadata, not every screen's text.
Null has a fixed dark appearance and right-handed navigation: retain the
unavailable controls with a clear disabled explanation where appropriate.
Do not delete shared configuration keys or overwrite another theme's values.

### Reuse state and feedback, even when geometry changes

Battery charging/low-level states, active radios, playback state, EQ values and
volume commits must come from shared sources. A line may replace an arc without
creating a second charging state machine or a second volume-writing path.

## 5. Fast implementation sequence

### A. Write the theme brief

Record the theme ID, palette, font stack, supported appearances/handedness,
backgrounds, navigation model and unique features. Add the completeness table.
List intentional restrictions now, rather than explaining omissions later.

### B. Register and inherit first

1. Start from a known working revision; record it as the comparison baseline.
2. Add palettes, font assets/licences and a `theme_def_t` with the smallest kit.
3. Add a stable preset identity and expose it in the picker. Current picker
   visibility/order is also maintained in `ui/settings.c`; registration alone
   does not guarantee it is offered.
4. Add C/font sources to the build. Do not renumber existing saved preset IDs.
5. Render inherited screens before adding custom layouts.

The first milestone is a complete, functioning theme with inherited layouts.

### C. Build the distinctive screens

Usually Music, navigation and QS establish the look. Keep their data/action
models shared. Then apply the same visual vocabulary to settings, lists,
connections, track info, volume, EQ, battery, standby and popups.

Use existing artwork preparation/caching. Null's pixelated cover is a cached
presentation of artwork, not a reason to decode or pixelate every frame.
Update on track/art/settings changes, not on every position tick.

### D. Add a dedicated options section

Put theme-only settings under `Settings → Display → <Theme> Options`, alongside
the existing Disco/Null sections. Keep shared playback, audio, network, system
and Help settings in their common homes.

Prefer this order when the options exist:

1. Menu items and Quick items.
2. Palette and Background.
3. Title hold and playback controls.
4. QS layout, Fonts and Music text alignment.
5. Optional Music information and navigation behaviour.
6. Advanced appearance/motion choices.

Use `<theme>_...` keys for presentation preferences. Keep shared preferences
shared; isolate hint learning when experience with one theme would otherwise
hide guidance for a new interaction model. Map a shared control to the correct
key in one place, as the current `setting_key()` does for Null's hint mode.

For each option specify its default, scope, apply mode (live/rebuild/restart),
validation, disabled reason and save-failure behaviour. Reuse the standard
selector/help layout. Non-boolean settings that open a page show a chevron;
On/Off and actual read-only values remain visible.

### E. Check the differences, then package

Use focused checks for the changed components and the boundary paths below. Do not rerun a broad suite repeatedly for wording or colour-only tweaks.
Document what was rendered, what was exercised interactively and what still
requires a physical Disc. Build/source/patch must identify the same revision.

## 6. Lightweight acceptance checks

### Visual pass

- At native 360×360, check circular edges, longest labels, large font settings,
  symbols, international text, empty data and loading/error states.
- Selectors stay centred; larger explanatory text remains separate and readable.
- Library names abbreviate at rest and scroll only when focused. Long Music
  metadata runs three cycles on track change, then rests.
- Check the actual state colours: playing/paused, radios, charging and low battery.
- Render every supported appearance, handedness and materially different layout.
  A fixed-dark/right-only theme instead needs its disabled explanations checked.

### Existing-theme compatibility pass

- Record affected themes and approved baseline revisions.
- Confirm their fonts, palette, geometry and motion have not changed.
- Confirm all implemented actions, routes, defaults and saved preferences remain.
- Compare only the shared components touched by the new work, expanding checks
  when a difference appears. Keep unrelated old-theme redesigns out of the patch.

### Interaction pass

- Play/pause, previous/next, mode and scrubbing; hold/release must not double-fire.
- Drawer open/close, item selection and configured double-tap/hold shortcuts.
- QS add/remove/reorder, empty selection, reopen and displayed action mapping.
  Check both text/icons if both exist, plus theme preference isolation.
- Popup Back, How to → Help → Settings, library track selection, Immersive return
  and wake all keep the active theme.
- Reopen settings; verify intended persistence and a failed save's rollback.

The host renderer includes focused selectors such as `REVIEW_QS_PARITY`,
`REVIEW_HELP_MENU`, `REVIEW_240` and `REVIEW_NULL_BATTERY`. Read their code before
using them: some older review branches contain assumptions from earlier layouts.
Do not present a passing screenshot render as a full interaction or device test.

### Existing commands

Run from the repository root:

```bash
make -s -C ui check-theme
python3 ui/tools/gen_theme_presets.py --check
make -s -C ui CROSS= host-render -j2
```

A native preview uses this argument order:

```text
ui/tests/host_build/host-render PRESET VARIANT preview-PAGE 0 OUTPUT.ppm
```

The current fixture path expects artwork beside the output (`OUTPUT.png`);
metadata fixtures may additionally need a matching FLAC. See the renderer's
preview branch rather than assuming it runs without fixtures.

Keep native and MIPS objects separate. Use the existing device-build helper:

```bash
python3 ui/tools/build_device.py \
  --toolchain /absolute/path/to/mipsel-n2008-musl-cross \
  --cache /absolute/path/to/device-build-cache \
  --out /absolute/path/to/new-release-directory \
  --jobs 2
```

The helper checks the static MIPS32r2 / FP64 / NaN2008 binary format; that is not
physical-device verification. If an interrupted build left empty objects or a
zero-byte executable, remove the incomplete outputs and rebuild before claiming
success. An empty executable can otherwise appear to exit successfully.

## 7. Release handoff

- Record the baseline, source commit, binary hash and relevant settings changes.
- Supply the matching `mq_ui`, source and patch/bundle requested for that release.
- Verify that a binary patch reconstructs the packaged tree from its baseline;
  verify bundle prerequisites and the advertised branch.
- Update theme documentation, Help/gesture descriptions and website screenshots
  from the actual implementation. Use a consistent font/palette/background set.
- Separate native checks, ABI checks and physical testing in the release notes.
- A documentation-only change does not require rebuilding the UI.

## 8. New-theme handoff template

Copy this into the theme's implementation notes:

```markdown
# <Theme name>
- ID / baseline:
- Visual brief and native-size references:
- Palette / font sources / licences / fallback chain:
- Supported appearance and handedness:
- Shared features inherited:
- Custom views and their shared action/data owners:
- Intentional restrictions and disabled explanations:
- Existing-theme compatibility baseline and opt-in boundaries:
- Theme Options: keys, defaults, scope and apply mode:
- Help topics and context hints:
- Lifecycle / invalidation / artwork caching:
- Completeness table status:
- Native renders and focused interaction results:
- Device build / physical testing status:
- Deliverable revision and hashes:
```

A theme is ready when it looks coherent, preserves the shared action contracts,
returns through every route correctly, and has no unexplained blanks in its
completeness table—not merely when Music looks finished.
