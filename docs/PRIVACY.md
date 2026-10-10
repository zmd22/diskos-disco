# diskOS privacy and network disclosure

This lists everything the installer and the diskOS UI (`mq_ui`) send over the network. The UI source
is published under GPL-3.0-or-later in [`ui/`](../ui/); this disclosure is based on that code and on
observed behaviour. It is a good-faith summary, not a guarantee - read the source for the last word.

## The installer (this repo)

- **`./install.sh` uses `pip`** to upgrade `pip` itself and download two packages - `pyusb` and
  `pycryptodome` - from **PyPI** into a local `.venv`. That `pip`/PyPI traffic is the only network
  activity in setup. Point `pip` at your own mirror if you prefer, or pre-install the packages offline.
- **The installer app itself makes no network requests.** Building the image, decrypting/extracting
  your firmware, saving the recovery image, and flashing over USB are all fully local. It does not
  phone home and sends no telemetry.

## The diskOS UI (`mq_ui`) on the device

diskOS is a local music player. Weather on Home is **on by default**. While it is on, diskOS fetches the weather when the UI starts, retries after failures, and refreshes later. With Wi-Fi connected, this can contact `wttr.in` without opening the Weather app. Turning off Settings > Display > Online & Extras > Weather on Home stops automatic startup, retry and refresh fetches and hides the glance. Changing or resetting the location in the Weather app explicitly requests weather even while that toggle is off. A request already in flight may still finish after you turn it off. Other online features act under the conditions below. Each request reveals the device's public IP to the service it contacts.

| Feature | Endpoint | Protocol | What is sent |
|---|---|---|---|
| **Weather** | `wttr.in` | **plain HTTP** | With Weather on Home on, diskOS fetches at UI startup and later refreshes or retries when Wi-Fi works. With it off, automatic fetches stop, but changing or resetting location in the Weather app explicitly requests weather. A request already in flight may still finish after you turn it off. It sends your configured location, or no location so wttr.in can estimate one from your public IP. HTTP traffic is visible on the network. |
| **Online album art** | `itunes.apple.com` | HTTPS | When Online Album Art is enabled and a playing track has no cover, track and album metadata are used to look for artwork. Off by default. |
| **Lyrics** | `lrclib.net` | HTTPS | The current track's **title and artist**, to find matching lyrics. |
| **Scrobbling (Last.fm)** | `ws.audioscrobbler.com` | HTTPS | If enabled and connected: the device sends `track.updateNowPlaying` and scrobbles with track metadata (artist/title/album/timestamp), API key and session key to **your** Last.fm account. |
| **Update check and download** | `api.github.com`, `github.com` | HTTPS | Opening Update diskOS checks the latest release. The 1.2.0 release image includes the diskOS release key by default; confirming a supported update downloads its signed app bundle. Switching Allow diskOS Updates off prevents download and staging but still permits a release check. The first update over Wi-Fi will be a release after 1.2.0. |
| **Last.fm sign-in** | `ws.audioscrobbler.com/2.0/`, `www.last.fm/api/auth/` | HTTPS | The device POSTs `auth.getToken` and polls `auth.getSession` at `https://ws.audioscrobbler.com/2.0/` with the API key and authorization token. It displays a QR approval URL; your phone opens that URL. `www.last.fm/api/account/create` is shown as text so you know where to get an API key. |

### Last.fm credential setup happens over your LOCAL network in plaintext

To connect Last.fm, diskOS runs a **temporary web server on the device** at
`http://<device-wifi-ip>:8080/<random-token>/`, shows a QR for it, and your phone posts your Last.fm
**API key and shared secret** to it. Important properties:
- It is **plain HTTP on your Wi-Fi LAN** - your API key/secret cross your local network unencrypted.
  Do this on a network you trust.
- The server is **transient** (runs only during setup), bound to the Wi-Fi interface, and gated by a
  random URL token.
- Your Last.fm **API key, secret, and session key are then stored on the device** under `/usr/data`
  (device config); **offline scrobbles are queued** in `/usr/data/lastfm.queue` until they can be sent.
  Nothing Last.fm-related is sent anywhere except your device, your phone (during setup), and Last.fm.

> **Last.fm is BETA and unverified end-to-end** - see the README's Current limitations. The transport and
> signing are tested, but a full connect-and-scrobble round-trip to a live account has not been.

### The stock FiiO player still runs alongside diskOS

diskOS replaces only the UI; FiiO's original player process still runs underneath it. That stock
component - not diskOS - is responsible for any Bluetooth, firmware-update checks, and LAN media
endpoints (DLNA/AirPlay/Roon/QPlay-style discovery) the device may present on your local network. Those
behaviours are inherited from the stock firmware and are outside diskOS's code. On V2.57, the stock player also performs an `ipinfo.io` time-zone lookup and network time synchronization after Wi-Fi connects when Automatic Time is enabled; changing that setting takes effect after the player next starts.

## What diskOS does NOT do

- No analytics, telemetry, or usage tracking.
- It does not upload your music library, your listening history (beyond Last.fm scrobbles if you turn
  them on), or any personal files.
- Weather on Home Off stops automatic fetches, including startup; changing or resetting location in the Weather app still requests weather. Online Lyrics starts on by default but requests occur only when the Lyrics page is opened for a song without local or embedded words. Online Album Art and Last.fm start off.

## Reporting

Report anything that looks like unexpected data leaving the device via the process in
[`SECURITY.md`](../SECURITY.md).
