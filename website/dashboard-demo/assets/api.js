// DEMO data source for the HiveHub dashboard.
//
// Drop-in replacement for the real server/dashboard/assets/api.js: it exposes the
// same `api` surface but serves deterministic, generated sample data entirely in
// the browser — no backend, no network. Used by the public "frontend demo" on the
// website so people can click through the dashboard without installing anything.
//
// Read methods return representative curves; the write methods (firmware /
// calibration) are intentionally disabled and reject with a friendly message so
// the UI shows "this is a read-only demo" instead of pretending to act.

const DEVICES = [
  {
    device_id: "demo-garden-01", display_name: "Garden Apiary 01",
    claimed_at: "2026-03-01T08:00:00Z",
    last_firmware_version: "0.23.9",
    channels: { scale_1: "North hive", scale_2: "South hive" },
    twoHives: true, hidden: false,
  },
  {
    device_id: "demo-rooftop-02", display_name: "Rooftop 02",
    claimed_at: "2026-04-15T08:00:00Z",
    last_firmware_version: "0.23.9",
    channels: { scale_1: "Rooftop hive", scale_2: null },
    twoHives: false, hidden: false,
  },
  {
    // A retired device, hidden from the hive picker by default — showcases the
    // admin "Visible devices" toggle. Toggling it in the demo actually works
    // (setDeviceVisibility below mutates this in memory).
    device_id: "demo-orchard-03", display_name: "Orchard 03 (retired)",
    claimed_at: "2025-06-01T08:00:00Z",
    last_firmware_version: "0.22.4",
    channels: { scale_1: "Orchard hive", scale_2: null },
    twoHives: false, hidden: true,
  },
];

// deterministic pseudo-noise in [-1, 1] from an integer seed
function noise(seed) {
  const x = Math.sin(seed * 12.9898) * 43758.5453;
  return (x - Math.floor(x)) * 2 - 1;
}

const DAY = 86400000;

// Base HiveHeart FFT profile (16 decoded relative levels, 0–15), shaped like a
// colony spectrum: energy concentrated in the low hum ranges (~188–375 Hz) and
// tapering toward the top of the 1.5 kHz band. Scaled by daytime activity and
// jittered per point in the demo series (see point()).
const HH_FFT_SHAPE = [6, 9, 12, 10, 7, 5, 4, 3, 2, 2, 2, 1, 1, 1, 1, 1];

function point(dev, t, i) {
  const dayPhase = 2 * Math.PI * ((t % DAY) / DAY); // 0..2π over a day
  const monthSwell = Math.sin((2 * Math.PI * t) / (DAY * 30));
  const seed = Math.floor(t / 600000) + (dev.device_id === "demo-rooftop-02" ? 9000 : 0);

  const hive1 = 35.1 + 0.55 * Math.sin(dayPhase) + 0.15 * noise(seed);
  const ambient = 16 + 9 * Math.sin(dayPhase - 0.6) + 3 * monthSwell + 0.6 * noise(seed + 1);
  const w1 = 42 + 3 * monthSwell + 0.8 * Math.sin(dayPhase) + 0.15 * noise(seed + 2);
  const w2 = 38 + 2.6 * monthSwell + 0.7 * Math.cos(dayPhase) + 0.15 * noise(seed + 3);
  const soc = Math.max(28, Math.min(100, 72 + 26 * Math.sin(dayPhase - 1.0)));
  // Wireless in-hive sensors slowly discharge over the demo window (a coin/
  // 18650 cell), with a little daily temperature ripple on the voltage.
  const days = Math.max(0, (Date.now() - t) / DAY); // 0 at newest, larger going back
  const scaleV = 3.75 - 0.02 * days + 0.02 * Math.sin(dayPhase) + 0.01 * noise(seed + 30);
  const blePct = Math.max(5, Math.min(100, 88 - 2.4 * days + 1.5 * noise(seed + 31)));
  const traffic = Math.max(0, 42 * Math.sin(dayPhase - 0.8));

  const m = {
    id: 1_000_000 - i,
    device_id: dev.device_id,
    measured_at: new Date(t).toISOString(),
    scale_1_weight_kg: w1, scale_1_weight_kg_compensated: w1,
    hive_1_temp_c: hive1,
    hive_1_humidity_percent: 55 + 5 * Math.sin(dayPhase + Math.PI) + 0.5 * noise(seed + 5),
    ambient_temp_c: ambient,
    ambient_humidity_percent: 60 + 12 * Math.sin(dayPhase + Math.PI) + 0.6 * noise(seed + 6),
    ble_1_pressure_hpa: 1013 + 5 * monthSwell + 0.5 * noise(seed + 7),
    battery_soc_percent: soc,
    battery_voltage: 3.6 + (soc / 100) * 0.6,
    battery_alert: false,
    rssi_dbm: -62 + 6 * Math.sin(dayPhase) + noise(seed + 8),
    network_transport: "wifi", cellular_ok: null, cellular_csq: null,
    time_source: "ntp", rtc_ok: true, calibration_mode: false,
    firmware_version: dev.last_firmware_version, boot_count: 14,
    mic_left_rms_dbfs: -43 + 4 * Math.sin(dayPhase) + 0.4 * noise(seed + 9),
    mic_right_rms_dbfs: -44 + 4 * Math.cos(dayPhase) + 0.4 * noise(seed + 10),
    mic_left_peak_dbfs: -20 + 3 * Math.sin(dayPhase),
    mic_right_peak_dbfs: -21 + 3 * Math.cos(dayPhase),
    mic_sample_rate_hz: 16000, mic_sample_frames: 1024,
    mic_left_band_sub_bass_dbfs: -55 + 2 * Math.sin(dayPhase),
    mic_left_band_hum_dbfs: -37 + 3 * Math.sin(dayPhase),
    mic_left_band_piping_dbfs: -60 + 2 * noise(seed + 11),
    mic_left_band_stress_dbfs: -50 + 2 * Math.sin(dayPhase),
    mic_left_band_high_dbfs: -70 + 2 * noise(seed + 12),
    mic_right_band_sub_bass_dbfs: -56 + 2 * Math.cos(dayPhase),
    mic_right_band_hum_dbfs: -38 + 3 * Math.cos(dayPhase),
    mic_right_band_piping_dbfs: -61 + 2 * noise(seed + 13),
    mic_right_band_stress_dbfs: -51 + 2 * Math.cos(dayPhase),
    mic_right_band_high_dbfs: -71 + 2 * noise(seed + 14),
    bee_counter_1_total_in: 12000 + Math.round((t / DAY) % 1000) * 4,
    bee_counter_1_total_out: 11850 + Math.round((t / DAY) % 1000) * 4,
    bee_counter_1_interval_in: Math.round(traffic + 4 * noise(seed + 15) + 4),
    bee_counter_1_interval_out: Math.round(traffic * 0.95 + 4 * noise(seed + 16) + 4),
    // Wireless in-hive sensors on hive 1: a BLE weighing scale (voltage) and a
    // HolyIot/Ruuvi environment beacon (percent).
    hivescale_1_battery_v: scaleV,
    ble_1_battery_percent: Math.round(blePct),
  };

  if (dev.twoHives) {
    m.scale_2_weight_kg = w2; m.scale_2_weight_kg_compensated = w2;
    m.hive_2_temp_c = hive1 - 0.4 + 0.2 * noise(seed + 17);
    m.hive_2_humidity_percent = 54 + 5 * Math.sin(dayPhase + Math.PI);
    m.bee_counter_2_total_in = 9800 + Math.round((t / DAY) % 1000) * 3;
    m.bee_counter_2_total_out = 9700 + Math.round((t / DAY) % 1000) * 3;
    m.bee_counter_2_interval_in = Math.round(traffic * 0.8 + 4 * noise(seed + 18) + 3);
    m.bee_counter_2_interval_out = Math.round(traffic * 0.78 + 4 * noise(seed + 19) + 3);
    // Hive 2 carries a wireless acoustic sensor (HiveHeart) on its own cell.
    m.hiveheart_2_battery_v = 3.68 - 0.02 * days + 0.015 * Math.sin(dayPhase) + 0.01 * noise(seed + 32);
    // Decoded HiveHeart 16-band FFT (relative levels 0–15), matching the
    // hiveheart_N_fft_bins the backend derives from the raw 8-byte array.
    const hhActivity = 0.6 + 0.4 * Math.sin(dayPhase); // busier by day
    m.hiveheart_2_fft_bins = HH_FFT_SHAPE.map((base, b) =>
      Math.max(0, Math.min(15, Math.round(base * hhActivity + 1.2 * noise(seed + 40 + b)))));
    // HiveHeart's independently-reported peak frequency (Hz), ~colony hum band.
    m.hiveheart_2_frequency_hz = 180 + 60 * Math.sin(dayPhase) + 10 * noise(seed + 60);
  }
  return m;
}

function findDevice(id) {
  return DEVICES.find((d) => d.device_id === id) || DEVICES[0];
}

// Generate a newest-first series spanning [start, now] (default 7 days).
function series(deviceId, startIso) {
  const dev = findDevice(deviceId);
  const now = Date.now();
  const start = startIso ? new Date(startIso).getTime() : now - 7 * DAY;
  const span = Math.max(DAY, now - start);
  const count = 320;
  const out = [];
  for (let i = 0; i <= count; i++) {
    const t = start + (span * i) / count;
    out.push(point(dev, t, i));
  }
  out.reverse(); // newest first, matching the real API
  return out;
}

// ── Hive audio (issue #71) ─────────────────────────────────────────────────
//
// The demo has no hub and no backend, so everything the audio panel asks for is
// answered from static files in assets/audio/ plus a clock. All of the pretence
// lives here on purpose: views.js is a verbatim copy of the real dashboard, so
// it must not learn that it is running in a demo.
//
// Two things are faked, differently:
//
//   * **Stored sessions** are honest. The files really are played by the same
//     <audio> element the real dashboard uses; only the metadata around them is
//     invented. The list is filtered to the files that actually exist, so an
//     empty assets/audio/ degrades to "No recordings yet" rather than a row of
//     broken players.
//   * **Live listening** is a simulation. A real session streams PCM off a hive
//     over BLE; here a sample file is decoded once and served back in
//     real-time-sized slices through exactly the protocol the panel expects
//     (offset in, PCM out, 204 for "nothing yet"). It looks like the real thing
//     because it goes through the real code path — which is also why the panel
//     below is labelled as a simulation in the demo README.

const AUDIO_DIR = "assets/audio/";
const AUDIO_RATE = 16000;   // what the real node records at; slices are sized for it

// The sample recordings the demo offers. `file` names what you drop into
// assets/audio/ — see the README there. Entries whose file is absent are hidden,
// so this list is a menu of what the demo *can* show, not a promise that it will.
//
// The second entry deliberately carries damage. A recording with a hole in it
// plays as perfectly continuous sound, and the warning the dashboard puts above
// it is the only thing that tells a listener not to trust the join — so the demo
// shows that state rather than pretending every session is clean.
const DEMO_RECORDINGS = [
  {
    id: 9001, file: "hive-clean.mp3", hive_index: 1, minutesAgo: 42,
    seconds: 12.0, dropped_bytes: 0, gaps: 0, clipped_pct: 1, complete: true,
    crc_ok: true, requested_by: "demo",
  },
  {
    id: 9002, file: "hive-incomplete.mp3", hive_index: 2, minutesAgo: 190,
    seconds: 9.4, dropped_bytes: 3840, gaps: 2, clipped_pct: 7, complete: false,
    crc_ok: false, requested_by: "demo",
  },
];

// Which sample files are actually present. Resolved once, lazily: a HEAD per
// candidate on first use, cached thereafter.
let audioPresence = null;
async function presentRecordings() {
  if (!audioPresence) {
    audioPresence = Promise.all(DEMO_RECORDINGS.map(async (r) => {
      try {
        const res = await fetch(AUDIO_DIR + r.file, { method: "HEAD" });
        return res.ok ? r : null;
      } catch (_) {
        return null;  // file:// origins reject HEAD; treat as absent
      }
    })).then((rows) => rows.filter(Boolean));
  }
  return audioPresence;
}

function recordingRow(r) {
  const at = new Date(Date.now() - r.minutesAgo * 60000).toISOString();
  return {
    id: r.id, device_id: "demo", hive_index: r.hive_index, status: "ready",
    requested_at: at, started_at: at, completed_at: at,
    requested_duration_s: 0, gain_db: 0, sample_rate: AUDIO_RATE,
    bytes: Math.round(r.seconds * AUDIO_RATE * 2), seconds: r.seconds,
    dropped_bytes: r.dropped_bytes, gaps: r.gaps, clipped_pct: r.clipped_pct,
    complete: r.complete, crc_ok: r.crc_ok, error: null,
    requested_by: r.requested_by,
  };
}

// ── The simulated live session ─────────────────────────────────────────────
//
// Decode a sample file to 16-bit PCM once, then hand it back at the rate the
// node would have produced it. The panel polls by byte offset and stops when
// the recording reports "ready" with nothing left, so honouring those two
// signals is all it takes to drive the real UI end to end.
let livePcm = null;      // Int16Array of the decoded sample
let liveSession = null;  // { id, startedAt }

async function decodeSample() {
  if (livePcm) return livePcm;
  const rows = await presentRecordings();
  if (!rows.length) return null;
  const res = await fetch(AUDIO_DIR + rows[0].file);
  if (!res.ok) return null;
  const Ctx = window.AudioContext || window.webkitAudioContext;
  const buf = await new Ctx().decodeAudioData(await res.arrayBuffer());
  const src = buf.getChannelData(0);
  const out = new Int16Array(src.length);
  for (let i = 0; i < src.length; i++) {
    const v = Math.max(-1, Math.min(1, src[i]));
    out[i] = v < 0 ? v * 0x8000 : v * 0x7fff;
  }
  livePcm = out;
  return livePcm;
}

// How many PCM bytes "exist" this many ms into the simulated session. The
// sample loops, so a 12-second file still supports the full 60-second cap the
// real node enforces.
function liveBytesAt(ms) {
  return Math.floor((ms / 1000) * AUDIO_RATE) * 2;
}

const demoErr = () =>
  Promise.reject(new Error("This is a read-only demo — firmware and calibration actions are disabled."));

const wrap = (v) => new Promise((r) => setTimeout(() => r(v), 120)); // tiny latency for realism

export const api = {
  listDevices: () => wrap(DEVICES.map(({ twoHives, ...d }) => ({
    ...d, last_seen_at: new Date().toISOString(),
  }))),

  measurements: (deviceId, { start } = {}) => wrap(series(deviceId, start)),

  latest: (deviceId) => wrap([series(deviceId)[0]]),

  config: (deviceId) => wrap({
    device_id: deviceId, send_interval_seconds: 900,
    claim_code: "DEMO-1234",
    scale1_offset: 12345, scale1_factor: 21.3,
    scale2_offset: 12010, scale2_factor: 21.7,
    config_version: 4, tempco_enabled: true, tempco_source: "hive_1",
    tempco_ref_temp_c: 25.0, scale1_tempco_kg_per_c: -0.012, scale2_tempco_kg_per_c: -0.011,
    beecounter_night_mode_enabled: true, beecounter_night_start_minute: 21 * 60,
    beecounter_night_end_minute: 5 * 60 + 30, beecounter_night_max_traffic: 60,
    // All three emitter banks live: the counter is running its whole 24-gate
    // entrance, which is the default and what the demo device does.
    beecounter_bank1_enabled: true, beecounter_bank2_enabled: true,
    beecounter_bank3_enabled: true,
    timezone: "CET-1CEST,M3.5.0,M10.5.0/3",
  }),

  channels: (deviceId) => {
    const dev = findDevice(deviceId);
    return wrap({
      scale_1_display_name: dev.channels.scale_1,
      scale_2_display_name: dev.channels.scale_2,
    });
  },

  insightsSummary: (deviceId) => wrap({
    device_id: deviceId, computed_at: new Date().toISOString(),
    alert_count: 1, highest_severity: "warning",
    highest_alert: {
      severity: "warning", title: "Possible nectar dearth",
      message: "Weight has been roughly flat for 3 days during the expected flow — consider checking forage.",
    },
    categories: { swarming: 0, activity: 1, overwintering: 0 },
  }),

  insightsHistory: (deviceId, { status = "all" } = {}) => {
    const now = Date.now();
    const alerts = [
      {
        alert_key: "activity.nectar_dearth", status: "active", severity: "warning",
        peak_severity: "warning", title: "Possible nectar dearth",
        description: "Weight has been roughly flat for 3 days during the expected flow — consider checking forage.",
        first_seen_at: new Date(now - 3 * DAY).toISOString(),
        last_seen_at: new Date(now).toISOString(), resolved_at: null,
      },
      {
        alert_key: "power.battery_low", status: "resolved", severity: "info",
        peak_severity: "warning", title: "Battery running low",
        description: "State of charge dipped below 35 % during a cloudy spell.",
        first_seen_at: new Date(now - 12 * DAY).toISOString(),
        last_seen_at: new Date(now - 9 * DAY).toISOString(),
        resolved_at: new Date(now - 9 * DAY).toISOString(),
      },
    ].filter((a) => status === "all" || a.status === status);
    return wrap({ device_id: deviceId, alerts });
  },

  firmwareStatus: (deviceId) => wrap({
    device_id: deviceId, target: "hivescale", current_version: "0.23.9",
    latest_version: "0.23.11", latest_is_official: true,
    approved_version: null, update_available: true, pending_approval: true,
  }),

  // Hiding a device works in the demo (mutates the in-memory list) so the
  // "Visible devices" toggle can be tried out; the picker updates live.
  setDeviceVisibility: (deviceId, hidden) => {
    const dev = DEVICES.find((d) => d.device_id === deviceId);
    if (dev) dev.hidden = !!hidden;
    return wrap({ device_id: deviceId, hidden: !!hidden });
  },

  // Sample account list so the admin "Dashboard users" panel renders with data.
  listUsers: () => wrap([
    { id: 1, username: "demo", role: "admin", email: "demo@example.com" },
    { id: 2, username: "viewer", role: "viewer", email: null },
  ]),

  // Sample notification config so the "Alert notifications" card renders both
  // channels as configured; the actual enable/test actions stay read-only below.
  notificationsConfig: () => wrap({
    email_enabled: true, web_push_enabled: true,
    min_severity: "warning", vapid_public_key: "demo",
  }),

  // write actions are disabled in the demo
  uploadFirmware: demoErr,
  importSdData: demoErr,
  approveFirmware: demoErr,
  queueHiveInsideUpdate: demoErr,
  queueBeeCounterUpdate: demoErr,
  startProvisioning: demoErr,
  startCalibration: demoErr,
  stopCalibration: demoErr,
  fitTempCompensation: demoErr,
  updateConfig: demoErr,
  updateChannels: demoErr,
  deleteMeasurements: demoErr,
  // Device lifecycle: erasing a device and re-seeding one to HivePal both need a
  // real server (and a real claim code), so they surface the read-only notice.
  deleteDevice: () =>
    Promise.reject(new Error("This is a read-only demo — deleting a device needs a HiveHub server.")),
  reseedDevice: () =>
    Promise.reject(new Error("This is a read-only demo — re-seeding a device to HivePal needs a HiveHub server.")),
  // account management is auth-backed in the real dashboard; disabled here
  createUser: demoErr,
  deleteUser: demoErr,
  changePassword: demoErr,
  updateEmail: demoErr,
  testNotification: demoErr,
  // Download / backup: the demo's readings are generated in the browser, so
  // there is nothing on a server to export. Rejecting here is what makes the
  // panel show its "read-only demo" notice and keep the download disabled.
  exportSummary: () =>
    Promise.reject(new Error("This is a read-only demo — the data download is disabled.")),
  exportUrl: () => "#",

  // ── Publish data ──────────────────────────────────────────────────────────
  // The metric registry is served so the Publish form is fully explorable, and
  // the list starts empty. Publishing itself needs a server to mint the public
  // token, so it surfaces the same read-only notice as the other write actions.
  publishMetrics: () => wrap([
    { id: "weight", label: "Weight", unit: "kg", digits: 2, scope: "hive" },
    { id: "hive_temp", label: "Hive temperature", unit: "°C", digits: 1, scope: "hive" },
    { id: "hive_humidity", label: "In-hive humidity", unit: "%", digits: 0, scope: "hive" },
    { id: "sound_rms", label: "Sound level (RMS)", unit: "dBFS", digits: 1, scope: "hive" },
    { id: "bees_in", label: "Bees in (per interval)", unit: "", digits: 0, scope: "hive" },
    { id: "bees_out", label: "Bees out (per interval)", unit: "", digits: 0, scope: "hive" },
    { id: "ambient_temp", label: "Ambient temperature", unit: "°C", digits: 1, scope: "device" },
    { id: "ambient_humidity", label: "Ambient humidity", unit: "%", digits: 0, scope: "device" },
    { id: "battery_soc", label: "Collector battery", unit: "%", digits: 0, scope: "device" },
    { id: "battery_voltage", label: "Collector battery voltage", unit: "V", digits: 2, scope: "device" },
    { id: "solar_power", label: "Solar power", unit: "mW", digits: 0, scope: "device" },
    { id: "rssi", label: "Signal strength", unit: "dBm", digits: 0, scope: "device" },
  ]),
  publishedCharts: () => wrap([]),
  publishChart: () =>
    Promise.reject(new Error("This is a read-only demo — publishing needs a HiveHub server.")),
  updatePublishedChart: demoErr,
  deletePublishedChart: demoErr,

  // ── Hive audio ───────────────────────────────────────────────────────────

  // Shaped exactly like the server's: { recordings: [...] }. The panel reads
  // res.recordings, so a bare array here would read as "no recordings" forever.
  listRecordings: async () => ({
    recordings: (await presentRecordings()).map(recordingRow),
  }),

  recording: async (id) => {
    if (liveSession && id === liveSession.id) {
      const elapsed = Date.now() - liveSession.startedAt;
      // The real node stops itself at 60 s; the simulation ends at the same
      // point, so the "keep listening?" prompt fires exactly as it would.
      const done = elapsed >= 60000;
      return {
        id, device_id: "demo", hive_index: liveSession.hive, status: done ? "ready" : "streaming",
        requested_at: new Date(liveSession.startedAt).toISOString(),
        requested_duration_s: 0, gain_db: 0, sample_rate: AUDIO_RATE,
        bytes: liveBytesAt(Math.min(elapsed, 60000)), seconds: Math.min(elapsed, 60000) / 1000,
        dropped_bytes: 0, gaps: 0, clipped_pct: 0, complete: done, crc_ok: true,
        error: null, requested_by: "demo",
      };
    }
    const rows = await presentRecordings();
    const row = rows.find((r) => r.id === id);
    if (!row) throw new Error("recording not found");
    return recordingRow(row);
  },

  requestRecording: async () => {
    const pcm = await decodeSample();
    if (!pcm) {
      // Honest refusal rather than a session that plays nothing: the demo needs
      // a sample file before it can pretend to hear a hive.
      throw new Error(
        "This demo has no sample audio yet — add a file to "
        + "website/dashboard-demo/assets/audio/ (see the README there).");
    }
    liveSession = { id: 9100 + (Date.now() % 800), startedAt: Date.now(), hive: 1 };
    return { id: liveSession.id, status: "requested", hive_index: 1, duration_s: 0 };
  },

  recordingPcm: async (id, offset) => {
    if (!liveSession || id !== liveSession.id) {
      return { bytes: null, nextOffset: offset, status: "ready" };
    }
    const pcm = await decodeSample();
    const elapsed = Math.min(Date.now() - liveSession.startedAt, 60000);
    const available = liveBytesAt(elapsed);
    if (!pcm || offset >= available) {
      // 204 in the real API: nothing new yet, which is the common answer while
      // polling and is not an error.
      return { bytes: null, nextOffset: offset, status: "streaming" };
    }
    const out = new Int16Array((available - offset) / 2);
    const total = pcm.length;
    for (let i = 0; i < out.length; i++) {
      out[i] = pcm[(offset / 2 + i) % total];  // loop the sample past its end
    }
    return {
      bytes: out.buffer,
      nextOffset: available,
      status: elapsed >= 60000 ? "ready" : "streaming",
    };
  },

  recordingWavUrl: (id) => {
    const row = DEMO_RECORDINGS.find((r) => r.id === id);
    return row ? AUDIO_DIR + row.file : "";
  },

  deleteRecording: () =>
    Promise.reject(new Error(
      "This is a read-only demo — deleting a recording needs a HiveHub server.")),
};
