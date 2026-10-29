# Flashing and switching interfaces: what stays, what goes

[Back to the README](../README.md) · [How-to](HOWTO.md) · [SD updates](UPDATING.md)

> **Best-effort hobby project, experimental, as-is.** No warranty or liability; not affiliated with FiiO or Snowsky.
> Back up your music and keep the saved stock image before you install.

## Quick answers

**I installed Disco, but the Disc still shows FiiO's normal screen.**
Turn the Disc off. Hold **Volume Up** and turn it on. Disco appears. Then go to
**Settings > System > Controls > Default UI** and choose **diskOS**. Done: you don't need to install again.

**How do I switch between Disco and FiiO's normal screen?**
- Just once: turn the Disc off, hold **Volume Up**, turn it on.
- Always: in Disco, **Settings > System > Controls > Default UI**.

**Will I lose my music?**
No. Installing, updating or going back never touches your music.

**I reinstalled Disco and my old settings came back. Is that normal?**
Yes. The Disc remembers them, even when Disco is installed again or removed.

**I updated FiiO's firmware and Disco is gone.**
That's normal: a FiiO update replaces Disco. Make sure the Disc is on version **V2.57**, then install Disco again.
Your settings come back.

**Something looks wrong after trying another version (like upstream diskOS).**
In Disco: **Settings > System > Maintenance > Reset diskOS Settings**. Your music, Wi-Fi, Bluetooth and sound
settings stay.

**The computer can't find my Disc when installing.**
Turn the Disc off, hold **Volume Down**, then plug in the cable. The screen stays black; that's normal. Use a
proper data cable (some cables only charge) and plug it straight into the computer.

**Can I delete the `.diskos` or `diskos-update` folders on my SD card?**
Yes, it's safe. They're rebuilt or only needed during an update.

**Do I need to keep anything on my computer?**
Yes: the backup the installer made of your Disc. You need it to go back to FiiO's original system. Don't delete it.

---

## The details

Most surprises around installing, going back to stock or trying upstream diskOS come from one fact: **the Disc keeps
your data in a separate place that flashing doesn't touch.** This page explains the three places, what each action
changes, and what to do in the common situations.

## The three places

| Place | What's in it | Changed by |
|---|---|---|
| **The system** | FiiO's firmware, and Disco's boot scripts and interface once installed. Also the update key you flashed with. | Flashing (Disco's or upstream's installer, restore stock) and FiiO's own firmware updates replace it completely. An SD update replaces only the interface. |
| **The data area** (`/usr/data`) | Your settings, the **Default UI** choice, the queue, battery history, SongSphere, apps, SD-update state, Last.fm queue; also FiiO's library database with your listening history and play counts. | **Nothing in the list above.** Flashing, restoring stock and firmware updates all leave it as it is. |
| **The SD card** | Your music. Disco also adds `.diskos/` (a cover cache and Album Roulette data, rebuilt automatically) and the `diskos-update/` folder you copy for SD updates. | Only you. Deleting `.diskos/` or `diskos-update/`, or using another card, is harmless. |

So after any reinstall, Disco comes back **with your old settings**, and a choice like "Default UI: Stock" still applies.

## What each action does

| Action | Disco on the Disc | Your settings / Default UI | Update key | History, music |
|---|---|---|---|---|
| **Install (flash) Disco** | Installed fresh | Kept | Replaced by the key you flash with (none: SD updates off) | Kept |
| **SD update** | Interface replaced, boot scripts unchanged | Kept | Unchanged | Kept |
| **Default UI > Stock** | Stays installed, just not started | Kept | Unchanged | Kept |
| **Hold Volume Up at power-on** | Starts the *other* interface for this one boot | Kept | Unchanged | Kept |
| **Restore stock** (installer) | Deactivated; its files in the data area stay, unused | Kept, unused | Gone with the system | Kept |
| **FiiO firmware update** | Gone (the system is replaced) | Kept, unused | Gone | Kept |
| **Reset diskOS Settings** (in Disco) | Unchanged | Interface settings back to defaults; music, Wi-Fi, Bluetooth, Last.fm, EQ and audio settings stay | Unchanged | Kept |

## Common situations

**It keeps starting stock after installing.** Almost always an earlier **Default UI: Stock** choice (made in Disco or
in upstream diskOS), which survives flashing. Hold **Volume Up** while powering on to start Disco once, then set
**Settings > System > Controls > Default UI** to **diskOS**. No need to flash again.
If Volume Up doesn't bring Disco up either, Disco didn't start, and the Disc fell back to stock (it always does when
Disco can't start). Flash again from stock V2.57; if it keeps happening, the installer's log and
`/usr/data/diskos_install.log` (over SSH, once Disco runs) show why.

**Is Disco installed?** Settings > Help > About in Disco shows its version. Stock's own settings show FiiO's version.

**I want to try upstream diskOS (or come back from it).** Go back to stock first, then install the other one. Your
Disco settings stay in the data area meanwhile, and upstream may leave its own there too: if something looks or
behaves oddly afterwards, use **Settings > System > Maintenance > Reset diskOS Settings**. Each one also brings its
own update key, so after switching back to Disco, SD updates need Disco installed with **your** key again.

**I updated the stock firmware and Disco is gone.** Expected: FiiO's update replaces the system. Make sure the Disc is
on **V2.57** (the only supported version), then install Disco again. Your settings come back with it.

**I lost my update key, or I'm on a new computer.** SD updates only install when signed with the key the Disc was
flashed with. Copy your `diskos-keys` folder to the new computer, or, if it's lost, make new keys and flash once
with the new public key. Settings and music stay.

**I want Disco gone completely.** Restore stock with the installer. Disco's files stay in the data area but do
nothing without Disco; they're small. To start from a clean slate later, run **Reset diskOS Settings** in Disco
*before* restoring stock.

**The installer can't find the Disc.** Flashing mode: turn the Disc off, hold **Volume Down**, then connect USB. The
screen stays **black**: that's normal. Use a data cable (not a charge-only one) and a port directly on the computer.

## Keep the stock backup safe

The installer saves your Disc's original system on the **computer you flashed from**, and needs it to restore stock:

- Linux: `~/.local/share/diskos-installer`
- macOS: `~/Library/Application Support/diskos-installer`
- Windows app: `%LOCALAPPDATA%\diskOS-Disco\backup`

Keep a copy somewhere else as well. **The installer's `remove` command deletes these saved files**, so restore stock
(or copy the backup away) before you remove the installer from that computer.
