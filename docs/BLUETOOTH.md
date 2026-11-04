# Bluetooth audio (experimental)

[Back to the README](../README.md) · [Settings](DISPLAY_SETTINGS.md) · [3.4.0 notes](releases/v3.4.0.md)

> [!WARNING]
> **Experimental.** Bluetooth audio, the codec choice, aptX, the automatic switching between Bluetooth stacks, the
> crash guard and Quiet Wi-Fi for Bluetooth are all experimental. They were tested on one Disc with two pairs of
> headphones. Wired listening is the dependable way to use the Disc.

Pair and connect headphones or speakers in **Settings › Connectivity › Bluetooth**. Audio follows the connected
headphones automatically.

## Codecs

**Settings › Connectivity › Bluetooth › Bluetooth Codec** applies from the next connection:

| Choice | What the headphones get |
|---|---|
| **aptX/LDAC Mobile / Standard / High** | LDAC or aptX, whichever they have, then AAC, then SBC: LDAC → aptX HD → aptX → AAC → SBC. Mobile, Standard and High are LDAC's quality (aptX has none). |
| **AAC** | AAC, else SBC. Never higher than AAC (useful if LDAC stutters). |
| **SBC** | SBC, which every pair supports. |

Each pair of headphones gets its own best codec: an LDAC pair gets LDAC and an aptX pair aptX, with the same
setting. Disco learns each pair's codecs at its first connection. **Song Info** shows the codec really in use, for
example *Bluetooth · LDAC*.

## aptX and aptX HD

FiiO's Bluetooth audio on the Disc has SBC, AAC and LDAC only. Disco carries its own: BlueALSA 4.0.0 with the
open-source libfreeaptx encoder, built for the Disc and stored inside the interface, so an SD update brings it.

1. Turn on **Settings › Connectivity › aptX (experimental)**. Disco puts its files in place and says when it's ready.
2. Choose a **Bluetooth Codec** starting with *aptX/LDAC*.
3. Turn Bluetooth off and on.

The first connection of new headphones can still use SBC while Disco learns their codecs; from then on they get their
best one. Off (the default), Bluetooth is FiiO's, exactly as in 3.3.1.

- **LDAC always runs on FiiO's Bluetooth audio**, aptX on Disco's: LDAC through Disco's build froze the Disc in testing.
  When a pair's best codec needs the other one, Disco pauses the music and reconnects the headphones once (*Reconnecting
  for LDAC…* / *…for aptX…*). Press Play again afterwards.
- **aptX Adaptive and aptX Lossless are not supported:** there is no open-source encoder for them. Headphones with
  those (and classic aptX) get classic aptX.

## When a codec doesn't work

- A codec that doesn't come up steps down to the next one for **that connection only**. The next connection tries
  the best one again. Nothing is remembered as broken.
- **Crash guard:** if the Disc restarts just as music starts on a codec that hasn't played on those headphones before,
  that codec is skipped for them and Disco says so. Choose Bluetooth Codec again to give it another try.

## Quiet Wi-Fi for Bluetooth

The Disc's Wi-Fi and Bluetooth share one 2.4 GHz radio and take turns. **Settings › Connectivity › Quiet Wi-Fi for
Bluetooth** (on by default): while audio goes to Bluetooth, Wi-Fi saves power and stops searching for networks when it
isn't connected, so the headphones get more airtime. LDAC needs the most, so it gains the most. Wi-Fi still works;
everything goes back to normal when audio leaves Bluetooth.

## Known limits

- After changing headphones, press **Play** again.
- Dropouts on LDAC: keep Quiet Wi-Fi on, or try **aptX/LDAC Mobile** (about half LDAC Standard's bitrate).
- Bluetooth codec choice applies to firmware V2.57 only.
