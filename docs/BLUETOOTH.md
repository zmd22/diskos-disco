# Bluetooth audio (experimental)

[README](../README.md) · [Settings](DISPLAY_SETTINGS.md) · [3.4.0 notes](releases/v3.4.0.md)

> [!WARNING]
> Bluetooth audio, codec selection, aptX, stack switching, the crash guard and Quiet Wi-Fi are experimental, tested
> on one Disc with two headphones. Wired is the reliable path.

Pair in **Settings › Connectivity › Bluetooth**. Audio follows connected headphones automatically.

## Codecs

**Settings › Connectivity › Bluetooth › Bluetooth Codec**, applied on next connection:

| Choice | Order tried |
|---|---|
| **aptX/LDAC Mobile / Standard / High** | LDAC → aptX HD → aptX → AAC → SBC. The suffix sets LDAC quality. |
| **AAC** | AAC → SBC (useful if LDAC stutters). |
| **SBC** | SBC only. |

Each headphone gets its best supported codec, learned on first connection. **Song Info** shows the active codec
(e.g. *Bluetooth · LDAC*).

## aptX and aptX HD

FiiO's stack supports SBC, AAC and LDAC only. Disco bundles BlueALSA 4.0.0 with libfreeaptx inside `mq_ui`, so it
ships via SD update.

1. Enable **Settings › Connectivity › aptX (experimental)** and wait for the ready message.
2. Pick a **Bluetooth Codec** starting with *aptX/LDAC*.
3. Toggle Bluetooth off and on.

A new headphone's first connection may use SBC while its codecs are learned. With the switch off (default), Bluetooth
is FiiO's stack, unchanged from 3.3.1.

- **LDAC runs on FiiO's stack, aptX on Disco's** (LDAC on BlueALSA froze the Disc in testing). When switching is
  needed, Disco pauses and reconnects once (*Reconnecting for LDAC…* / *…for aptX…*); press Play afterwards.
- **No aptX Adaptive or Lossless** (no open-source encoder); such headphones get classic aptX.

## Fallback

- A codec that fails steps down for that connection only; the next connection retries the best one.
- **Crash guard:** if the Disc restarts as a never-before-played codec starts on a headphone, that codec is skipped
  for it and Disco tells you. Re-select the Bluetooth Codec to retry.

## Quiet Wi-Fi for Bluetooth

Wi-Fi and Bluetooth share one 2.4 GHz radio. **Settings › Connectivity › Quiet Wi-Fi for Bluetooth** (default on)
enables Wi-Fi power saving and stops scanning while disconnected during Bluetooth playback, freeing airtime (LDAC
benefits most). Wi-Fi stays usable; normal behaviour resumes when audio leaves Bluetooth.

## Known limits

- Press **Play** after switching headphones.
- LDAC dropouts: keep Quiet Wi-Fi on, or use **aptX/LDAC Mobile** (~half LDAC Standard's bitrate).
- Codec selection requires firmware V2.57.
