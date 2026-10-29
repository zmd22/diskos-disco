> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Unified settings validation

Source update for dev. The approved reusable hierarchy is in SETTINGS_TEMPLATE.md.

## Checks

- Host renderer builds successfully.
- Seven themes / twelve appearances pass shared roots, ordering, every visible submenu handler, nonempty destinations, Back paths and preference preservation.
- Equalizer opens the existing profile screen; Details opens the existing battery screen; DSD is read-only and sends no player command.
- Retained settings keep the same saved keys, bounds, option arrays, handlers and defaults.
- Theme efficiency checks preserve animation frames and Home, Music and Quick Settings pixels against the previous dev baseline. Only intentional settings-root snapshots have a separate approved fixture.
- Idle shutdown, shutdown flow, audio-running and plugged-in detection host regressions pass.
- Known baseline limitation: REVIEW_HINTS fails its song/album overlay assertion on the pre-change host binary too. Remaining focused Display cases are run separately.
- No MIPS toolchain or connected player is available here. The tracked release binary is unchanged; physical playback, charging and power-off are not certified by these host checks.

## Compiled contents

### Stone

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Stone settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Charging Limit |
| Theme settings | Appearance → Music screen → Screensaver |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Now Playing style |
| Appearance | Accent colour → Outdoor mode |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Standby | Screensaver style |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Ring

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Ring settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Charging Limit |
| Theme settings | Appearance → Music screen → Screensaver |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Now Playing style |
| Appearance | Accent colour → Outdoor mode |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Standby | Screensaver style |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Braun

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Braun settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Charging Limit |
| Theme settings | Appearance → Music screen |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Now Playing style |
| Appearance | Accent colour → Outdoor mode |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Disco

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Disco settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Battery percentage → Charging Limit |
| Theme settings | Appearance → Music screen → Navigation → Quick settings layout |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Controls → Information → Progress → Text |
| Appearance | Accent colour → Equalizer colours → Volume colours → Outdoor mode → Rim sheen |
| Music info | Clock and info → Show clock → Track number → Track times |
| Theme navigation | Gestures → List motion → Menu items → Navigation side |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| Quick settings | Layout |
| Clock and info | Weather |
| Music text | Text alignment → Track font |
| Gestures | Menu double tap → Menu hold → Title hold |
| Music progress | Progress colour → Progress shape |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About → Interaction hints → Show hints again |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Null

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Null settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Battery percentage → Charging Limit |
| Theme settings | Appearance → Music screen → Navigation → Quick settings layout |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Background → Controls → Information → Text |
| Appearance | Colour scheme → Menu colours |
| Gestures | Menu double tap → Menu hold → Title hold |
| Quick settings | Layout |
| Music text | Text alignment → Track font |
| Music info | Clock and info → Show album → Show clock → Track number |
| Theme navigation | Gestures → List motion → Menu items → Menu timeout |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| Clock and info | Weather |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About → Interaction hints → Show hints again |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Squash

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Squash settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Battery percentage → Charging Limit |
| Theme settings | Appearance → Music screen → Navigation |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Music screen | Background → Information → Progress → Text |
| Appearance | Equalizer colours → Volume colours → Outdoor mode |
| Music info | Clock and info → Playback mode → Playback state → Show clock → Track number → Track times |
| Theme navigation | Gestures → List motion → Menu items |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| Clock and info | Weather |
| Music text | Text alignment → Track font |
| Gestures | Menu double tap → Menu hold → Title hold |
| Music progress | Progress colour → Progress shape |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About → Interaction hints → Show hints again |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |

### Zune

| Page | Ordered rows |
| --- | --- |
| Display | Brightness → Theme → Zune settings → Immersive → Library appearance → Navigation → Text & layout |
| Bluetooth | Devices → Bluetooth Codec |
| Battery & power | Details → Idle Power-off → Restart → Screen-off delay → Screensaver delay → Shut down player → Sleep timer → Charging Limit |
| Theme settings | Appearance → Navigation → Screensaver |
| Theme navigation | Main icon → Main icon double-tap → Main icon hold → Menu hold |
| Playback | Play Mode → Equalizer → Artists → Library seek → Resume Playback → Auto-tag → Play Through Folders → Up Next → SongSphere |
| Audio | Balance → DAC Filter → DSD Output → Gain → Max Volume → ReplayGain → Working Mode → DRE → Gapless |
| Appearance | Accent colour |
| Connectivity | Wi-Fi → Bluetooth → MA Sendspin → Online services |
| Navigation | Back-swipe distance → Quick settings items → Shortcuts |
| Text & layout | Text size → Screen rotation → Screen transitions |
| Date & Time | Set Date & Time → Time Zone → 24-hour time → Automatic Time |
| Library appearance | Album view → Album track numbers |
| Immersive | Style → Control action → Circular control → Immersive lyrics |
| Standby | Clock Accent → Lock Screen |
| Online services | Artwork cache → Online album art → Online lyrics → Weather |
| System | Language → Controls → Date & Time → Library management → Maintenance |
| Help | How to → About |
| Sleep timer | Duration → When Sleep Ends |
| Library | Rescan Library → Import Playlists |
| Controls | Volume Keys → Default UI |
| Maintenance | Update from SD Card → Debug Mode → Reset diskOS Settings |
| About | About → Device Info → Temperature |
| Volume Keys | Press → Double Press → Long Press |
| MA Sendspin | Server → Player Name → Sync Delay → Sendspin |
