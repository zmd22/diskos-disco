# Disco 3.2.2 tour

A native LVGL interface for the 360 × 360 Snowsky Disc. The stock music player continues to handle audio; Disco provides browsing, controls and its additional apps. This describes the release, not later development changes. See [settings](DISPLAY_SETTINGS.md) for exact menu ownership.

## Getting around

Disco uses an edge shard that opens a navigation circle; select a destination from the circle. Left-handed mode mirrors the navigation. Null uses its own right-side menu; Squash uses a rising launcher; Zune uses a scrolling main menu. Theme-specific menu and gesture assignments are under Display > the selected theme's settings > Navigation.

Use the edge Back gesture or the available Back control to return. Quick Settings opens with a top-edge pull. With QS style Shade, screens other than Music first show a cover, track details, transport, progress, clock and battery; pull again or tap the handle for Quick Settings. Swipe up to dismiss. Shade animates after the gesture; it does not continuously follow the finger in 3.2.2.

## Music and Immersive

Music presents artwork, track identity, playback state and seek controls according to the theme. Disco offers button, auto-hide and gesture controls. Squash uses single-tap track actions, double-tap playback and horizontal skip gestures on the track text. Null has its own controls and pixel presentation. Zune has its own Music layout and volume control.

The round track menus in Disco/Null/Squash provide Favourite, Lyrics, Album, Artist, View queue, Info, +Queue and +Playlist. Library long-press menus adapt to the selected song/group/file. Song Info shows metadata and an Auto-tag action.

[Immersive](IMMERSIVE_CONTROLS.md) supports Vinyl, CD and Cover, lyrics and transport overlay, plus optional circular seeking or volume. Theme navigation and the Ring Now Playing layout are separate concepts.

## Library

Browse songs, artists, albums, genres, favourites, history, playlists and folders. Artists can group by Artist or Album Artist. Play and Shuffle actions operate on the chosen list. Album presentation supports List, Wall and Coverflow.

Disco/Null/Squash list motion includes Wheel. Rows scale and fade with distance from the centre, follow the round edge and snap; Queue/radio action controls stay clear of scrolling rows. An album, or an artist's albums, opens with its cover / Play / Shuffle row just under the screen header and the first songs below. Library seek selects A–Z keys or the letter wheel. Empty letters are dimmed; the wheel moves through letters clockwise/counter-clockwise.

Search has a landing page, recent searches, keyboard and explicit search action. Results support artists and songs. Books and chapter navigation are separate from ordinary album browsing.

## Queue and Up Next

Queue is the explicit list of songs added to play next; Up Next exposes the player's playback context. Add songs or groups from action menus. Queue supports playback, reorder, shuffle and clear. Do not treat the two screens as interchangeable merely because both show upcoming music.

## Sound and connections

Equalizer provides player presets and editable user profiles where supported. Audio settings include gain, filter, ReplayGain, balance, volume limit and supported output controls. DSD mode is player-managed information.

Wi-Fi and Bluetooth have connection/detail screens. Bluetooth codec preference is firmware-gated; requested codec and actually negotiated codec are not equivalent. MA Sendspin is experimental; its audio path and behaviour should be checked on the intended Music Assistant setup.

## Power and standby

Screensaver and screen-off timing are in Battery & power. Display sleep is distinct from idle power-off. Idle shutdown is suppressed during playback, Sendspin audio, active Disc Shelf work, nonlocal working modes and detected external power. Since 3.2.1, charger detection remains latched until evidence of battery discharge; a computer USB connection also counts.

The explicit Sleep timer can pause or shut down when it expires. Holding the hardware power button saves statistics and battery history and starts a card flush before the stock player's own power-off. This source behaviour is not a guarantee against interruption or storage failure.

## Weather

Weather has current conditions and forecast; its service switch is under Connectivity > Online services. Theme clock decorations and weather fetching are separate settings.

## Listening Stats

Listening activity is stored locally and shown by week, month and year with highlights. Recording a track start is not proof of listening through the entire album. Stats does not yet provide the proposed rating-based smart-playlist feature.

## SongSphere

SongSphere is built in. Open its preview, then the app. Users manually place/tag music by mood and choose a position to generate a playlist. Track/group mood actions can enter the relevant editor directly. Playback > SongSphere controls availability.

## Album Roulette

Built into mq_ui. Its landing screen starts a random album selection and animated wheel; choose an album to play. A separate old Album Roulette installation is not needed.

## Boomshine

The built-in chain-reaction game runs as a child mode of mq_ui and temporarily owns the screen/input. It has its own drawing code rather than the normal LVGL theme kit.

## Disc Shelf

Built-in Navidrome/SMB3 browsing and original-file downloading. Configure a server, browse and select items, then download to the card. It does not continuously synchronise while closed. Use [the setup guide](DISC_SHELF_INSTALL.md) for credentials, replacement policy and experimental SMB limits.

## Apps in the menu

Installed external apps can be selected as navigation destinations/shortcuts. A preview separates browsing past an app from opening it. External apps own their own UI; Disco's theme engine does not automatically reskin them.

## Settings and updates

The shared roots are Playback, Audio, Display, Connectivity, System, Battery & power and Help. See [the settings reference](DISPLAY_SETTINGS.md) and [signed SD updates](UPDATING.md). UI-only updates do not replace the boot chain or the stock player. Keep a matching source/payload record and use About to identify the running binary.
