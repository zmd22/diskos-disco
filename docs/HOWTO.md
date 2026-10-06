# diskOS Disco! how-to

Short answers for everyday tasks. For a walk through every screen, see the [tour](DISCO_TOUR.md).

[Home](../README.md) | [Tour](DISCO_TOUR.md) | [Docs](README.md)

## Getting around

**Go back.** Swipe right from the left edge of the screen. A bubble follows your finger and turns your accent
colour once the swipe will count. Or tap a ‹ arrow in a header. Hold the arrow to go straight to Music.

**Get back to Music from anywhere.** Swipe up from the bottom edge, or hold the navigation circle.

**Switch sections.** Tap the shard on the right edge, then tap the top or bottom of the circle (or swipe
on it) until you see the section you want, and tap its middle. Tap outside the circle or its › to close it.

**Change what's in the navigation circle.** Settings > Display > Disco Options > Disco Menu. Use the arrows to
move a section, × to remove it, and Add to bring one in. Music always stays first; Music and Settings can't be
removed.

**Open Quick Settings.** Pull down from the top edge; swipe up to close.

## Playing music

**Play a song.** Library > Songs (or an album, artist, genre or playlist) and tap the song. Song lists have
Play and Shuffle at the top.

**Seek.** On Music, drag or tap the progress line under the title (or the arc around the rim).

**Change shuffle / repeat.** On Music, tap the small icon between previous and next. Each tap moves to the next
mode and a toast names it: Sequential, Shuffle, Repeat One, Repeat All, Single.

**Favourite the playing song.** Tap the title on Music, then the heart.

**Jump to the playing song's album or artist.** Tap the title on Music, then Album or Artist. The song (or its
album) is highlighted in the list.

**Full-screen cover.** Hold the title on Music, or tap it and choose Immersive (or use the Immersive shortcut).
Tap to leave.

**Favourite with one hold.** Disco Options > Title Hold > Favourite: holding the title then adds or removes the
song from Favourites (a toast confirms).

**Lyrics.** Tap the title on Music, then Lyrics. Lyrics come from an .lrc file beside the song, the song's own
tags, or (with Display > Online Lyrics on) an online lookup.

**Resume the last song after a restart.** Settings > Playback > Resume Playback: **Song** reopens the last song,
**Position** also returns to where you were.

## Queue

**Play something next.** Hold a song, album, artist, genre, file or folder and choose **Add to queue**. Queued
songs play after the current one, in the order you added them; then your music continues where it was.

**See or change the queue.** Pick Queue in the navigation circle, tap the title on Music and choose Queue, or
use the Queue shortcut. Drag a song by ≡ to move it, tap it to play it now, or tap Clear twice to empty the
queue.

## Search

**Find a song or artist.** Pick Search in the navigation circle and tap **Search library**. Type, then press
**Search** (typing alone doesn't search). Artists come first: tap one to open their albums. Songs follow: tap
to play, hold for the song's menu.

**Search again for something recent.** Your last three searches are on the Search landing page; tap one.

## Volume and sound

**Set the volume on screen.** Press a volume key; the volume pops up. Drag the arc on the right rim. Tap
anywhere else to close it.

**Limit the volume.** Settings > Audio > Max Volume.

**Pick an EQ preset.** Open EQ (navigation circle or a shortcut) and use the arrows. Off and the player's
built-in presets are listed first, then your own USER1–USER10.

**Make your own EQ.** On a USER preset, tap Edit. Tap a band to select it, then drag it up or down, or use −
and +. Tap the frequency under the faders to change the band's frequency instead of its gain. Double-tap a band
to reset it to 0 dB. Changes save to that preset.

**Rename a preset.** On the EQ screen, choose a USER preset and tap Rename.

**Use the Disc as a USB DAC, Bluetooth DAC, USB stick or AirPlay speaker.** Pick Working mode in the navigation
circle (or Mode in Quick Settings) and tap the mode. For USB storage, eject the card on your computer before
going back to Local playback.

## Make it yours

**Change the theme.** Settings > Display > Theme: Disco, Ring, Braun or Stone. Appearance switches between Dark
and Light; Automatic Appearance does it for you (light by day).

**Change the accent colour.** Settings > Display > Accent Colour. **Follow album** takes the colour from each
cover; or pick a fixed one.

**Make the rainbow parts use your accent instead.** Settings > Display > Disco Options: set Equalizer, Volume or
Progress Bar to **Accent**. Progress Bar can also be **Off**.

**Progress around the rim.** Disco Options > Progress Shape > Arc. **Title from the left:** Disco Options > Title
Position > Left.

**Hide the clock on Music.** Disco Options > Show Clock. **Hide the times under the progress line:** Disco
Options > Track Times. **Turn off the rainbow rim:** Disco Options > Rim Sheen.

**Bigger text.** Settings > Display > Font Size > Large.

**Choose your shortcuts.** Settings > Display > Shortcuts. Tap one of the five slots and pick what it opens.

**Make the back swipe easier or harder.** Settings > Display > Back-swipe (how far you have to swipe).

## Library upkeep

**Add new music.** Copy it to the card, then tap Rescan in Quick Settings (or Settings > System > Rescan
Library). A small dot runs around the rim while it scans.

**Import playlists.** Put .m3u or .m3u8 files on the card and choose Settings > System > Import Playlists.

**Edit tags, rename, move or delete files.** Library > Folders, hold the file or folder.

## Updates, recovery and power

<a id="update-from-the-sd-card"></a>
**Update from the SD card.**

1. On your computer, make a signed update with the signing script:
   `sh ui/tools/owner-keys/diskos-sign.sh path/to/mq_ui 1.0` (the last part is the version label shown on the Disc).
2. Copy the resulting `diskos-update` folder to the root of the SD card.
3. On the Disc: Settings > System > Update from SD Card > Update, then restart.
4. The new version starts as a **trial**. Play some music (or tap **Keep** when asked) to keep it. If it
   doesn't work out, it goes back to the previous version by itself; you can also choose **Go back**.

Updates are signed with your own keys, made once with `diskos-keys.sh`; the Disc only accepts updates signed with
the key it was installed with.

**See which version you're on.** Settings > System > About ("Disco! 1.0" plus the build ID).

**If a restart is refused.** The toast says why; for details, `cat /tmp/restart_last` over SSH (Debug Mode).

**Use FiiO's own interface.** Settings > System > Default UI > Stock. To boot the other interface just once,
hold Volume Up from power-on until it appears.

**If the screen shows FiiO's interface unexpectedly.** The Disco! interface stopped and the Disc fell back to the
stock one. Restart the Disc to return. Your music and settings are safe.

**Restart or switch off.** Settings > System > Restart, or Shut down player at the bottom of System.

**Sleep timer.** Settings > System > Sleep Timer; When Sleep Ends chooses pause or shut down.
