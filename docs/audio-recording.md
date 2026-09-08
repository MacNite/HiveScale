# Listening to a hive

A HiveInside node (firmware 0.6.0 or later) can be asked for live audio. The
hub relays it to the backend, and the dashboard plays it — while it is still
being recorded, or afterwards.

```
HiveInside                    HiveHub                     backend        browser
PDM mic → PCM16 → 32 KiB ring → BLE notify → 32 KiB ring → chunked POST → PCM/WAV
   16 kHz, 32 kB/s              137 kB/s measured          raw on disk    live or <audio>
```

This is the firmware-over-BLE relay in reverse, and deliberately so: the same
locate-scan, the same NimBLE client shape, the same crash-safe command
reporting. See [`ota-over-ble.md`](ota-over-ble.md) for the other direction and
HiveInside's `docs/audio-over-ble.md` for the wire protocol.

## Why raw PCM, and why streaming

Both answers come from one measurement. Notification throughput from a
HiveInside node to an ESP32-C6 was measured at **137 kB/s** (15 ms connection
interval, 2M PHY, 251-byte PDUs). The microphone produces **32 kB/s**.

* **No codec.** Compressing would be fitting a pipe that is not full. Worse, a
  lost ADPCM packet corrupts decoder state and everything after it, while a lost
  PCM packet is just a hole. The wire format keeps a `format` byte with ADPCM
  reserved, so this can change without a protocol break.
* **Streamed, not buffered.** The node holds ~1 second of audio, not a whole
  clip: buffering 60 s would be 1.9 MB of static RAM on a 512 KB part that also
  runs a BLE stack.

The OTA relay's ~1.4 kB/s is not a counter-example. That path writes with
response (two connection intervals per chunk) and flushes RRAM synchronously
inside the ATT callback, where each write runs in a radio timeslot that
pre-empts connection events. It measures flash scheduling, not the radio.

## Setting it up

**1. Provision the shared key.** The node will not record without an
authenticated request, and the hub will not ask without a key. Generate one:

```bash
openssl rand -hex 32
```

Put the same 64-character value in both places, each of them gitignored:

* HiveHub: `HIVEINSIDE_AUDIO_PSK_HEX` in `firmware/include/secrets.h`
* HiveInside: `hive_audio_psk` in `firmware-nrf54lm20a/src/audio_secret.h`

A missing or all-zero key fails closed on both sides. That is the intended
behaviour, not a bug to work around: the alternative is a microphone in a garden
that any BLE device in range can switch on.

**2. Flash both.** HiveHub with `HIVEINSIDE_AUDIO_ENABLED` (default on wherever
`ENABLE_BLE_SCAN` is), HiveInside with 0.6.0 or later.

**3. Mount the storage.** `RECORDINGS_DIR` (default `/app/recordings`) needs a
volume — the compose file ships one. Without it the database keeps rows whose
audio vanished on the next container recreation.

## Using it

**Dashboard → Audio → "Listen to a hive".** Pick a hive, press Listen. Then the
part that surprises people:

> **It is not instant.** The hub deep-sleeps and only collects commands on its
> next wake, so listening starts within one reporting interval. The panel says
> "Waiting for the hub to wake" for exactly this reason.

Once audio starts, the session runs for at most 60 seconds — the node's own cap.
At 50 seconds the dashboard asks whether to keep listening; saying yes queues
another session. That works smoothly because the hub deliberately stays awake
for `HIVEINSIDE_AUDIO_FOLLOWUP_MS` (25 s) after an audio session, so the second
request does not have to wait for another wake cycle. Say no, or ignore it, and
the hub goes back to its normal cycle.

Earlier sessions are in the dropdown below, newest first, and play as ordinary
WAV files.

## Reading the quality flags

A session can produce audio that is worth hearing and still not be whole. The
dashboard says so rather than letting you find out by ear, because a recording
with a hole in it plays as perfectly continuous sound — and an acoustic
judgement made on a splice is wrong in a way nothing later reveals.

| Flag | Meaning |
|---|---|
| `dropped_bytes` | The node's own ring overran — audio never left the hive |
| `gaps` | Sequence jumps on the air, or the hub's ring overran |
| CRC mismatch | What arrived is not what the node computed: corruption in transit |
| `clipped_pct` | Samples hit the rail; lower the gain |

Audio either side of a gap is real. The join is not.

## Storage

Recordings are raw 16 kHz mono PCM16 on disk. The WAV header is generated per
request, so the same bytes serve both the live player and a `.wav` download, and
a recording still being written never needs its header rewritten.

**Nothing prunes them.** There is no retention sweep — a deliberate choice — so
the only thing between a self-hosted disk and a season of audio is the delete
button. One minute is about 1.9 MB.

## API

Three surfaces, the same functions behind each: master key
(`/api/v1/devices/…`, `/api/v1/recordings/…`), dashboard session
(`/api/v1/local/…`), HivePal user (`/api/v1/app/…`).

| Endpoint | Purpose |
|---|---|
| `POST …/devices/{id}/commands/record-audio?slot=&duration=&gain_db=` | queue a session; `duration=0` is live |
| `GET …/devices/{id}/recordings` | list, newest first, optionally `?hive=` |
| `GET …/recordings/{id}` | one recording's metadata and quality flags |
| `GET …/recordings/{id}/pcm?offset=` | raw PCM from an offset — follows a live session; 204 means "nothing new yet" |
| `GET …/recordings/{id}/audio.wav` | the finished recording, playable anywhere |
| `DELETE …/recordings/{id}` | remove the row and its audio |

Requesting audio needs the admin role on the dashboard, or owner/admin in
HivePal. Listening to what already exists follows the usual per-device roles.

The hub's own ingest (`…/recordings/{id}/stream` and `/finalize`) is
device-authenticated and not for general use. Note that the hub builds those
URLs from its own device id and never from the command payload — a hub that
posted microphone audio to whatever address a queued command named would be one
malformed command away from streaming somebody's garden to a stranger.

## Privacy

Worth stating plainly, because it is easy to forget what this feature is: an
in-hive microphone in a garden can pick up human speech.

* Audio is stored unencrypted on your server.
* It is never part of public chart embeds and is not included in data exports.
* The BLE link is authenticated but not encrypted — the HMAC proves *who may
  ask*, not that the audio itself is private in flight. Someone with a sniffer
  in radio range could capture a session in progress.
* Recordings are kept until deleted, and the dashboard names who requested each.

## Troubleshooting

| Symptom | Cause |
|---|---|
| "no HiveInside audio key configured on this hub" | `HIVEINSIDE_AUDIO_PSK_HEX` is empty in `secrets.h` |
| "authentication failed" | The two keys differ, or the node has none |
| "audio service not found" | The node is running firmware older than 0.6.0 |
| "HiveInside not found in scan" | Out of range, flat battery, or busy with an OTA |
| Stuck at "Waiting for the hub to wake" | Normal for up to one reporting interval; longer means the hub is offline |
| "node busy" | An OTA or another session holds the node's single connection |
| Recording marked incomplete | See the quality flags above |
