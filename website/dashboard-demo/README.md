# HiveHub dashboard — public demo

A self-contained, backend-free copy of the built-in HiveHub dashboard (see
[`server/dashboard/`](../../server/dashboard/)) used for the website's
**"Live demo"** button. It runs entirely in the browser on deterministic sample
data, so anyone can click through every data group, switch devices/hives and
change the time range without installing the server.

- `index.html` — the dashboard shell plus a "demo with sample data" banner and a
  link back to the marketing site.
- `assets/api.js` — **demo-specific.** It generates representative sample
  measurements in-browser and disables the write actions — firmware,
  calibration, config/hive-name edits and account management all show a
  "read-only demo" notice.
- `assets/app.js` — **demo-specific fork** of the real controller: same
  selector/render wiring, but with the login/auth flow removed (the demo has no
  backend to sign in to). When the real `app.js` changes, port the relevant
  bits by hand.
- `assets/push.js` — **demo-specific stub** of the Web Push helpers. The demo
  has no service worker or backend, so it reports push as supported-but-off and
  the toggle surfaces the same "read-only demo" notice as the other write
  controls. Keep it in sync with the exports of `server/dashboard/assets/push.js`.
- `assets/{style.css,charts.js,format.js,views.js}` — **verbatim copies**
  of `server/dashboard/assets/*`.
- `assets/audio/` — **demo-specific.** Sample hive recordings for the audio
  panel, plus a README explaining what to drop in. Ships empty; the panel lists
  only the files that exist. A recording *request* is simulated by `api.js` —
  it walks through requested → streaming → ready in a few seconds and then
  serves one of those files — so `views.js` never learns it is running without
  a hub.

## Keeping it in sync

The shared assets are plain copies (there is no build step). After changing the
real dashboard, re-copy them:

```bash
cp server/dashboard/assets/{style.css,charts.js,format.js,views.js} \
   website/dashboard-demo/assets/
```

`index.html`, `assets/api.js`, `assets/app.js`, `assets/push.js` and
`assets/audio/` are demo-specific — don't overwrite those.

When the real dashboard gains a feature that calls a new `state.actions.*`
method, porting `views.js` alone is not enough: add the matching entry to the
demo's `app.js` and a stub to its `api.js`, or the view throws on render. The
audio panel is the worked example.

## Preview locally

```bash
cd website
python3 -m http.server 8080
# open http://localhost:8080/dashboard-demo/
```
