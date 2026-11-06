# Privacy and network use

Everything the installer and the on-device UI (`mq_ui`) send over the network. Based on the GPL source in
[`ui/`](../ui/) and observed behaviour; a good-faith summary, not a guarantee.

## Installer

- `./install.sh` runs `pip` to upgrade itself and install `pyusb` and `pycryptodome` from **PyPI** into `.venv`.
  That's the only network traffic in setup; use a mirror or preinstall offline if you prefer.
- The installer itself makes no network requests: image build, firmware extraction, backup and flashing are local.
  No telemetry.

## On-device UI

Every request exposes the Disc's public IP to the contacted service.

| Feature | Endpoint | Transport | Sent / when |
|---|---|---|---|
| **Weather** (default **on**) | `wttr.in` | **plain HTTP** | Your location, or none (wttr.in guesses from IP). Fetched at startup, on retry and periodically while Wi-Fi works, even without opening the app. Turning it off (**Connectivity › Online services › Weather**) stops automatic fetches; changing or resetting the location in the app still fetches. In-flight requests may complete. |
| **Online album art** (default off) | `itunes.apple.com` | HTTPS | Track/album metadata, when the playing track has no cover. |
| **Lyrics** (default on) | `lrclib.net` | HTTPS | Title and artist, only when the Lyrics page opens for a song without local/embedded lyrics. |
| **MA Sendspin** (experimental, default off) | Your Music Assistant server (LAN) | WebSocket, Noise-encrypted (Sendspin 1.0) | Player name, public key (player ID), Wi-Fi MAC, playback commands and timing. Auto mode uses local mDNS. Keys and pairings stay in `/usr/data/diskos-sendspin.keys`. Unpaired connections are encrypted but unauthenticated; pair (enter the shown code in MA) to rule out impostor servers. |
| **Disc Shelf** | Your Navidrome server or SMB3 share | Navidrome: per URL scheme. SMB3: encryption required. | Only while the app runs, one source at a time. Navidrome uses reusable salted tokens (prefer HTTPS). Remember password On stores credentials in plain text in a private config file. Old standalone builds send the raw Navidrome password. |
| **Last.fm scrobbling** (default off) | `ws.audioscrobbler.com` | HTTPS | `track.updateNowPlaying` and scrobbles (artist, title, album, timestamp) with API and session keys. |
| **Last.fm sign-in** | `ws.audioscrobbler.com/2.0/`, `www.last.fm/api/auth/` | HTTPS | `auth.getToken` / `auth.getSession` polling with API key and token. Approval happens on your phone via QR. `www.last.fm/api/account/create` is only displayed as text. |

### Last.fm setup uses plain HTTP on your LAN

Setup starts a temporary server at `http://<device-ip>:8080/<random-token>/` (Wi-Fi interface only, setup only);
your phone posts your **API key and shared secret** to it **unencrypted**. Use a trusted network. Key, secret and
session key are stored under `/usr/data`; offline scrobbles queue in `/usr/data/lastfm.queue`. Nothing goes anywhere
except the Disc, your phone and Last.fm.

> Last.fm is **beta**: transport and signing are tested, a full live scrobble round-trip is not.

### Stock FiiO player

The stock player still runs underneath and is responsible for Bluetooth, firmware-update checks and any LAN
discovery (DLNA, AirPlay, Roon, QPlay) the Disc advertises. On V2.57, with Automatic Time on, it also does an
`ipinfo.io` time-zone lookup and NTP sync after Wi-Fi connects (changes apply on next player start). None of this is
diskOS code.

## Not done

- No analytics, telemetry or usage tracking.
- No upload of your library, files or listening history (except Last.fm scrobbles if enabled).

## Reporting

Report unexpected network traffic via [`SECURITY.md`](../SECURITY.md).
