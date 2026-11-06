# Flashing and switching: what stays, what goes

[README](../README.md) · [How-to](HOWTO.md) · [SD updates](UPDATING.md)

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

## FAQ

| Question | Answer |
|---|---|
| Installed, but FiiO's screen still shows | Power on holding **Volume Up**, then **Settings › System › Controls › Default UI › diskOS**. No reinstall needed. |
| Switch between Disco and stock | Once: Volume Up at power-on. Permanently: **Default UI**. |
| Will I lose music? | No. Installing, updating and restoring never touch it. |
| Old settings returned after reinstall | Expected; they live in the data area. |
| FiiO update removed Disco | Expected. Get back to V2.57 and reinstall; settings return. |
| Odd after trying upstream diskOS | **Settings › System › Maintenance › Reset diskOS Settings** (keeps music, Wi-Fi, Bluetooth, audio). |
| Computer can't find the Disc | Off, hold **Volume Down**, plug in (screen stays black). Use a data cable directly on the computer. |
| Can I delete `.diskos/` or `diskos-update/` on the SD? | Yes. |
| Anything to keep on the PC? | The installer's stock backup. It's required to restore stock. |

## Storage areas

Flashing never touches the data area, which explains most surprises.

| Area | Contents | Changed by |
|---|---|---|
| **System** | FiiO firmware; Disco boot scripts, interface and update key once installed | Flashing (any installer, restore stock) and FiiO updates replace it. SD updates replace only the interface. |
| **Data** (`/usr/data`) | Settings, Default UI choice, queue, battery history, SongSphere, apps, update state, Last.fm queue, FiiO's library DB with play history | Nothing above. |
| **SD card** | Music; `.diskos/` (cover cache, Album Roulette data, auto-rebuilt); `diskos-update/` | Only you. |

So a reinstalled Disco keeps old settings, including "Default UI: Stock".

## Effect of each action

| Action | Disco | Settings / Default UI | Update key | History, music |
|---|---|---|---|---|
| Flash Disco | Fresh install | Kept | Replaced by the flashed key (none: SD updates off) | Kept |
| SD update | Interface replaced | Kept | Unchanged | Kept |
| Default UI › Stock | Installed, not started | Kept | Unchanged | Kept |
| Volume Up at power-on | Other UI, this boot only | Kept | Unchanged | Kept |
| Restore stock | Deactivated; data files remain unused | Kept, unused | Removed | Kept |
| FiiO firmware update | Removed | Kept, unused | Removed | Kept |
| Reset diskOS Settings | Unchanged | UI settings reset; music, Wi-Fi, Bluetooth, Last.fm, EQ, audio kept | Unchanged | Kept |

## Common situations

**Keeps booting stock.** Usually a leftover **Default UI: Stock** (from Disco or upstream). Volume Up at power-on,
then set Default UI to **diskOS**. If Volume Up doesn't help either, Disco failed to start and the Disc fell back
to stock; reflash from stock V2.57 and check the installer log or `/usr/data/diskos_install.log`.

**Is Disco installed?** **Settings › Help › About** in Disco shows its version.

**Trying upstream diskOS, or coming back.** Restore stock first, then install the other. Both leave data behind; use
**Reset diskOS Settings** if things look off. Each brings its own update key, so reflash Disco with **your** key to
get SD updates back.

**FiiO update removed Disco.** Return to V2.57 (the only supported version) and reinstall.

**Lost key or new computer.** SD updates must be signed with the flashed key. Copy `diskos-keys` over, or make new
keys and reflash once. Settings and music stay.

**Remove Disco completely.** Run **Reset diskOS Settings** first if you want a clean slate, then restore stock.
Leftover files are small and inert.

## Keep the stock backup

The installer stores the original system on the flashing computer:

- Linux: `~/.local/share/diskos-installer`
- macOS: `~/Library/Application Support/diskos-installer`
- Windows app: `%LOCALAPPDATA%\diskOS-Disco\backup`

Keep a second copy. The installer's `remove` command deletes these files, so restore stock or copy the backup first.
