# Install Disco 1.2.2 and Album Roulette

The release includes two separate executables: `mq_ui` (the firmware UI) and
`apps/album-roulette/app` (Album Roulette 1.3.1 with the memory fix). Roulette is
not embedded in mq_ui. Updating the UI alone does not install or update the app.
The menu entry, shortcuts and themed landing page remain in the firmware.

## Install or update the app

Close Album Roulette. Extract the complete release ZIP and open a terminal in
the extracted folder. Run these commands from that folder (or the repo root):

```sh
ssh root@192.168.178.63 'mkdir -p /usr/data/apps/album-roulette && cat > /usr/data/apps/album-roulette/.app-new && chmod 755 /usr/data/apps/album-roulette/.app-new && mv -f /usr/data/apps/album-roulette/.app-new /usr/data/apps/album-roulette/app' < apps/album-roulette/app
ssh root@192.168.178.63 'if [ ! -f /usr/data/apps/album-roulette/app.conf ]; then printf "name=Album Roulette\nexec=/usr/data/apps/album-roulette/app\n" > /usr/data/apps/album-roulette/app.conf; fi'
```

The executable is replaced only after transfer finishes. Existing configuration
and cached artwork are preserved. There is no source-folder app entry. Existing
users should also install this app update, even if they already installed 1.2.1.
No reboot is required for the app replacement; reopen Apps if its list was open.

## Update the firmware UI

From your diskos-disco repository, use your existing signing keys:

```sh
sh ui/tools/owner-keys/diskos-sign.sh /absolute/path/to/extracted-release/mq_ui 1.2.2-Disco
```

Copy the newly generated `diskos-update` folder to the SD card root. Choose
Settings > System > Update from SD Card, install and restart. Tap Keep when
prompted to accept the trial. For a full firmware installation, use the normal
installer with `--ui payload/mq_ui`, then install the user app above.

The resulting About version is Disco! 1.2.2. Add Album Roulette under
Settings > Display > Disco Menu. All entry points retain the landing screen.

## Memory fix

The app shares one fallback cover instead of allocating up to 37 placeholders.
It decodes before allocating the destination and keeps at most three uncached
decoded covers on the heap when disk caching fails. Cached covers remain
reclaimable file mappings. All 37 album choices and existing controls remain.
The decoder safety limits are unchanged; low RAM can still cause placeholders.
This release does not claim to eliminate all memory pressure on the device.

Source and licensing are included in the release ZIP and repository. The app
source archive contains the focused low-memory regression test.
