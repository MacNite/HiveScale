# Demo sample audio

Drop hive recordings here and the dashboard demo's **Audio → Listen to a hive**
panel will play them. The directory ships empty on purpose: nothing synthetic
should end up on the public site pretending to be a colony, so until you add a
file the panel honestly reports that there is nothing to hear.

## What to add

Two filenames are wired up in [`../api.js`](../api.js) (`DEMO_RECORDINGS`):

| File | Shown as |
|---|---|
| `hive-clean.mp3` | A clean 12-second session on hive 1 |
| `hive-incomplete.mp3` | A 9.4-second session on hive 2, flagged as damaged |

Either may be missing — the demo lists only the files that are actually there,
so one file alone is a perfectly good demo.

The second one is deliberately presented with dropped audio, gaps and a
checksum mismatch, so visitors see the warning the real dashboard puts above an
incomplete recording. **The file itself does not have to be damaged**; the
metadata lives in `api.js`. Use an ordinary recording and the demo will show
what the warning looks like. If you would rather it sound damaged too, splice a
couple of hundred milliseconds out of it.

## Format

**Compressed — `.mp3` or `.ogg`, mono, anything from 32 kbit/s up.** A 10-second
clip lands around 40–60 kB.

The real server stores and serves raw 16 kHz PCM16 inside a WAV, and a genuine
download is exactly that. The demo does not have to be: these files are played
by a plain `<audio>` element, which takes whatever the browser supports, and a
WAV of the same clip would be ~320 kB — every byte of which ships to every
visitor of the website. Bee audio survives 64 kbit/s mono without losing
anything a listener would notice.

To use `.ogg` (or a WAV, if you want the demo byte-faithful), change the `file:`
field of the matching entry in `api.js`.

Converting a real recording downloaded from a HiveHub server:

```bash
ffmpeg -i recording.wav -ac 1 -b:a 64k hive-clean.mp3
```

## What the Record button does

The demo has no hub, so a request is **simulated**: `api.js` walks it through
the same three states the real dashboard shows — requested, then streaming, then
ready — compressed into about eight seconds instead of a reporting interval, and
then plays the first available file as the result.

That means a visitor sees the real flow (ask, wait, listen) rather than a
mock-up of it, which is why `views.js` can stay a byte-for-byte copy of the
dashboard's.

With no file here at all, pressing **Record** refuses with a message pointing
back at this README rather than producing a request that ripens into silence.

## Keep it honest

These are marketing assets on a public page. Use your own recordings, and if you
ever add something synthesized, say so in the UI — a demo that passes a
generated tone off as a colony teaches visitors the wrong thing about what the
feature actually captures.
