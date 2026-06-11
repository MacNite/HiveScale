// accel.cpp — LIS3DH / LIS2DH12 low-frequency vibration capture + FFT.
#include "accel.h"

#if ENABLE_LIS3DH_ACCEL

#include <math.h>
#include <arduinoFFT.h>

namespace accel {

// ── Register map (LIS3DH and LIS2DH12 are identical for everything used here) ─
static constexpr uint8_t REG_WHO_AM_I   = 0x0F;
static constexpr uint8_t REG_CTRL_REG1  = 0x20;  // ODR + axis enables
static constexpr uint8_t REG_CTRL_REG4  = 0x23;  // BDU, full-scale, high-res
static constexpr uint8_t REG_STATUS     = 0x27;  // ZYXDA = new-data ready
static constexpr uint8_t REG_OUT_X_L    = 0x28;  // first data register

// Auto-increment bit for multi-byte reads (MSB of the sub-address).
static constexpr uint8_t AUTO_INCREMENT = 0x80;

// FFT size = sample count; require a power of two so no zero-padding is needed.
// 256 samples at 400 Hz ODR ≈ 640 ms capture, 1.56 Hz/bin — fine resolution for
// the 8–30 Hz swarm band (≈ 14 bins).
static constexpr size_t MAX_FFT_SIZE = 1024;

// ---------------------------------------------------------------------------
// Low-level I2C helpers
// ---------------------------------------------------------------------------
static bool writeReg(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t address, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(address);
  // Set the auto-increment bit so the device walks consecutive registers.
  Wire.write(len > 1 ? (uint8_t)(reg | AUTO_INCREMENT) : reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  size_t got = Wire.requestFrom((int)address, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

// Map a requested ODR in Hz to the CTRL_REG1 ODR nibble. Only the rates both
// chips support in normal mode are offered; anything else falls back to 400 Hz.
static uint8_t odrCodeFor(uint16_t hz) {
  switch (hz) {
    case 10:   return 0x2;
    case 25:   return 0x3;
    case 50:   return 0x4;
    case 100:  return 0x5;
    case 200:  return 0x6;
    case 400:  return 0x7;
    default:   return 0x7;  // 400 Hz
  }
}

static uint16_t odrHzFor(uint8_t code) {
  switch (code) {
    case 0x2: return 10;
    case 0x3: return 25;
    case 0x4: return 50;
    case 0x5: return 100;
    case 0x6: return 200;
    case 0x7: return 400;
    default:  return 400;
  }
}

// CTRL_REG4 full-scale field (FS[5:4]) and the matching high-resolution (12-bit)
// sensitivity in mg per digit (after right-shifting the 16-bit register value
// by 4). Values from the LIS3DH/LIS2DH12 datasheets.
static uint8_t fsBitsFor(uint8_t range_g) {
  switch (range_g) {
    case 4:  return 0x1 << 4;
    case 8:  return 0x2 << 4;
    case 16: return 0x3 << 4;
    default: return 0x0 << 4;  // ±2 g
  }
}

static float mgPerDigitFor(uint8_t range_g) {
  switch (range_g) {
    case 4:  return 2.0f;
    case 8:  return 4.0f;
    case 16: return 12.0f;
    default: return 1.0f;  // ±2 g, high-resolution
  }
}

// ---------------------------------------------------------------------------
// Band energy: RMS (in mg) of all FFT bins whose centre frequency falls in
// [loHz, hiHz]. Linear mg (not dBFS): the values are physical and meant to be
// compared and trended directly by the server-side insight detectors.
// magnitudes[] holds |X[k]| for k in [0, fftSize/2). normScale undoes the FFT
// + window gain so a 1 mg sinusoid in-band reads ~1 mg.
// ---------------------------------------------------------------------------
static float bandRmsMg(const double* magnitudes, size_t fftSize,
                       uint16_t sampleRate, uint16_t loHz, uint16_t hiHz,
                       double normScale) {
  double freqPerBin = (double)sampleRate / (double)fftSize;
  size_t binLo = (size_t)ceil((double)loHz / freqPerBin);
  size_t binHi = (size_t)floor((double)hiHz / freqPerBin);
  size_t nyquist = fftSize / 2;
  if (binLo < 1) binLo = 1;            // skip DC bin
  if (binLo >= nyquist) return 0.0f;
  if (binHi >= nyquist) binHi = nyquist - 1;
  double sumSq = 0.0;
  for (size_t b = binLo; b <= binHi; b++) {
    double m = magnitudes[b] / normScale;
    sumSq += m * m;
  }
  if (sumSq <= 0.0) return 0.0f;
  return (float)sqrt(sumSq);
}

// ---------------------------------------------------------------------------
// readSlot
// ---------------------------------------------------------------------------
bool readSlot(uint8_t address, AccelSnapshot& out) {
  out = AccelSnapshot{};

  // Probe WHO_AM_I so a missing accelerometer is a clean "not present" rather
  // than a bus error that stalls the cycle.
  uint8_t who = 0;
  if (!readRegs(address, REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VALUE) {
    Serial.printf("[ACCEL] 0x%02X not found (who=0x%02X)\n", address, who);
    return false;
  }

  const uint8_t odrCode  = odrCodeFor(LIS3DH_ODR_HZ);
  const uint16_t odrHz   = odrHzFor(odrCode);
  const uint8_t range_g  = LIS3DH_RANGE_G;
  const float mgPerDigit = mgPerDigitFor(range_g);

  // CTRL_REG1: ODR | XYZ enabled (normal mode, LPen=0).
  // CTRL_REG4: BDU=1 (block data update) | full-scale | HR=1 (high resolution).
  if (!writeReg(address, REG_CTRL_REG1, (uint8_t)((odrCode << 4) | 0x07)) ||
      !writeReg(address, REG_CTRL_REG4, (uint8_t)(0x80 | fsBitsFor(range_g) | 0x08))) {
    Serial.printf("[ACCEL] 0x%02X config write failed\n", address);
    return false;
  }

  out.present        = true;
  out.sample_rate_hz = odrHz;
  out.range_g        = range_g;

  // Clamp the requested sample count to a power of two we can FFT in RAM.
  size_t want = LIS3DH_SAMPLE_COUNT;
  size_t fftSize = 64;
  while (fftSize * 2 <= want && fftSize * 2 <= MAX_FFT_SIZE) fftSize *= 2;

  // Heap-allocate so we never blow the loopTask stack (same approach as mics).
  double* magnitude = (double*)malloc(fftSize * sizeof(double));  // vector mag samples
  double* vImag     = (double*)malloc(fftSize * sizeof(double));
  if (!magnitude || !vImag) {
    free(magnitude); free(vImag);
    Serial.println("[ACCEL] FFT heap alloc failed");
    return false;
  }

  // Let the just-(re)configured ODR settle, then drop the first few samples.
  const uint32_t samplePeriodUs = 1000000UL / odrHz;
  delay(20);

  double sum = 0.0;
  size_t n = 0;
  for (; n < fftSize; n++) {
    // Wait for ZYXDA (new sample) with a bounded timeout so a wiring fault can't
    // hang the cycle; fall back to a fixed delay if STATUS never flags ready.
    uint32_t startUs = micros();
    uint8_t status = 0;
    while (true) {
      if (readRegs(address, REG_STATUS, &status, 1) && (status & 0x08)) break;
      if (micros() - startUs > samplePeriodUs * 4 + 2000) break;
    }

    uint8_t raw[6];
    if (!readRegs(address, REG_OUT_X_L, raw, 6)) break;
    int16_t xr = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t yr = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t zr = (int16_t)((raw[5] << 8) | raw[4]);
    // 12-bit high-resolution: the value is left-justified in the 16-bit word.
    float xmg = (float)(xr >> 4) * mgPerDigit;
    float ymg = (float)(yr >> 4) * mgPerDigit;
    float zmg = (float)(zr >> 4) * mgPerDigit;

    double mag = sqrt((double)xmg * xmg + (double)ymg * ymg + (double)zmg * zmg);
    magnitude[n] = mag;
    sum += mag;
  }

  if (n < 64) {
    free(magnitude); free(vImag);
    Serial.printf("[ACCEL] 0x%02X only %u samples; skipping\n", address, (unsigned)n);
    out.sample_count = (uint16_t)n;
    return out.present;  // sensor present but capture too short for analysis
  }

  // Remove DC (gravity + mounting bias) so the bands reflect AC vibration only.
  double mean = sum / (double)n;
  double sumSq = 0.0, peakDev = 0.0;
  for (size_t i = 0; i < n; i++) {
    double ac = magnitude[i] - mean;
    sumSq += ac * ac;
    double dev = fabs(ac);
    if (dev > peakDev) peakDev = dev;
    magnitude[i] = ac;  // reuse buffer as the FFT real input
    vImag[i] = 0.0;
  }
  for (size_t i = n; i < fftSize; i++) { magnitude[i] = 0.0; vImag[i] = 0.0; }

  out.sample_count = (uint16_t)n;
  out.rms_mg  = (float)sqrt(sumSq / (double)n);
  out.peak_mg = (float)peakDev;

  ArduinoFFT<double> fft(magnitude, vImag, fftSize, (double)odrHz);
  fft.windowing(FFTWindow::Hann, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();  // magnitudes now in magnitude[0 .. fftSize/2 - 1]

  const double HANN_COHERENT_GAIN = 0.5;
  const double normScale = (fftSize / 2.0) * HANN_COHERENT_GAIN;

  out.band_swarm_mg    = bandRmsMg(magnitude, fftSize, odrHz, BAND_SWARM_LO_HZ,    BAND_SWARM_HI_HZ,    normScale);
  out.band_fanning_mg  = bandRmsMg(magnitude, fftSize, odrHz, BAND_FANNING_LO_HZ,  BAND_FANNING_HI_HZ,  normScale);
  out.band_activity_mg = bandRmsMg(magnitude, fftSize, odrHz, BAND_ACTIVITY_LO_HZ, BAND_ACTIVITY_HI_HZ, normScale);

  free(magnitude);
  free(vImag);

  Serial.printf("[ACCEL] 0x%02X ok: rms=%.1f peak=%.1f mg | swarm=%.2f fan=%.2f act=%.2f mg (%u@%uHz)\n",
                address, out.rms_mg, out.peak_mg, out.band_swarm_mg,
                out.band_fanning_mg, out.band_activity_mg,
                (unsigned)out.sample_count, (unsigned)odrHz);
  return true;
}

// ---------------------------------------------------------------------------
// writeSnapshotToJson
// ---------------------------------------------------------------------------
void writeSnapshotToJson(JsonDocument& doc, uint8_t slot, const AccelSnapshot& snap) {
  // Indexing with a temporary String makes ArduinoJson copy the key into its own
  // pool — the same pattern beecnt::writeSnapshotToJson uses. (A reused char[]
  // buffer would be stored zero-copy by pointer and alias every key.)
  String p = "accel_" + String((int)slot) + "_";

  doc[p + "ok"] = snap.present;
  // Only attach the analysis fields when we actually have a capture, so a
  // missing/!ok accelerometer leaves them null rather than 0 (which the server
  // and insight detectors would otherwise read as "perfectly still hive").
  if (!snap.present || snap.sample_count == 0) return;

  doc[p + "sample_rate_hz"] = snap.sample_rate_hz;
  doc[p + "sample_count"]   = snap.sample_count;
  doc[p + "range_g"]        = snap.range_g;
  doc[p + "rms_mg"]         = snap.rms_mg;
  doc[p + "peak_mg"]        = snap.peak_mg;
  doc[p + "band_swarm_mg"]    = snap.band_swarm_mg;
  doc[p + "band_fanning_mg"]  = snap.band_fanning_mg;
  doc[p + "band_activity_mg"] = snap.band_activity_mg;
}

}  // namespace accel

#endif  // ENABLE_LIS3DH_ACCEL
