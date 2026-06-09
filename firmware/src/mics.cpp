// mics.cpp — INMP441 capture + FFT implementation.
#include "mics.h"

#if ENABLE_INMP441_MICS

#include <math.h>
#include <arduinoFFT.h>

bool micsI2sInstalled = false;  // keep this global as before

// Handle for the RX channel created by the new (IDF 5.x) I2S driver. NULL until
// initMicsI2s() succeeds. The old driver was addressed purely by port number;
// the new one hands back an opaque channel handle that every call needs.
static i2s_chan_handle_t micsRxChan = nullptr;

// Finite dBFS value reported for a silent / unmeasurable band, instead of NAN.
// Mirrors the floor used by the broadband RMS/peak stats below.
static constexpr float MIC_SILENCE_DBFS = -200.0f;
 
// ---------------------------------------------------------------------------
// Helper: compute band energy from a magnitude spectrum.
// binFreq(n) = n * sampleRate / fftSize
// Returns the RMS energy of all bins whose centre frequency falls in [loHz, hiHz],
// expressed in dBFS relative to the same full-scale reference used for broadband RMS.
// Returns the silence floor (MIC_SILENCE_DBFS) when no bins fall in the range or
// the band is silent — never NAN. A raw NAN here would be serialized into the
// measurement JSON (as null with this ArduinoJson build, but as a literal "nan"
// token if NAN output is ever enabled, which is invalid JSON and would make the
// whole measurement POST fail validation server-side). The broadband RMS/peak
// stats already use this same finite floor, so the bands now match them.
// ---------------------------------------------------------------------------
// normScale converts a raw FFT bin magnitude back to a full-scale-relative
// amplitude. The time-domain samples are already divided by the ADC full scale
// before the transform (see computeBands), so here we only undo the FFT gain:
// a full-scale tone yields a peak-bin magnitude of ~(fftSize/2) * windowGain.
static float bandEnergyDbfs(const double* magnitudes, size_t fftSize,
                             uint32_t sampleRate,
                             uint32_t loHz, uint32_t hiHz,
                             double normScale) {
  double sumSq = 0.0;
  size_t count = 0;
  double freqPerBin = (double)sampleRate / (double)fftSize;
  size_t binLo = (size_t)ceil((double)loHz  / freqPerBin);
  size_t binHi = (size_t)floor((double)hiHz / freqPerBin);
  // Only use the first half of the spectrum (Nyquist)
  size_t nyquist = fftSize / 2;
  if (binLo >= nyquist) return MIC_SILENCE_DBFS;
  if (binHi >= nyquist) binHi = nyquist - 1;
  for (size_t b = binLo; b <= binHi; b++) {
    double m = magnitudes[b] / normScale;
    sumSq += m * m;
    count++;
  }
  if (count == 0 || sumSq <= 0.0) return MIC_SILENCE_DBFS;
  float rms = (float)sqrt(sumSq / (double)count);
  return (float)(20.0 * log10((double)rms));
}
 
// ---------------------------------------------------------------------------
// Helper: run arduinoFFT on a block of time-domain samples, fill bands.
// samples: array of FFT_SAMPLE_COUNT int32_t values (24-bit in MSB-shifted int32)
// ---------------------------------------------------------------------------
static void computeBands(const int32_t* samples, size_t count,
                          uint32_t sampleRate, double fullScale,
                          MicBands& out) {
  // arduinoFFT needs two double arrays; allocate on heap to avoid stack overflow.
  double* vReal = (double*)malloc(FFT_SAMPLE_COUNT * sizeof(double));
  double* vImag = (double*)malloc(FFT_SAMPLE_COUNT * sizeof(double));
  if (!vReal || !vImag) {
    free(vReal); free(vImag);
    Serial.println("[FFT] heap alloc failed");
    return;
  }
 
  size_t n = min(count, (size_t)FFT_SAMPLE_COUNT);
  for (size_t i = 0; i < n; i++) {
    vReal[i] = (double)samples[i] / fullScale;
    vImag[i] = 0.0;
  }
  // Zero-pad if we captured fewer than FFT_SAMPLE_COUNT samples
  for (size_t i = n; i < FFT_SAMPLE_COUNT; i++) {
    vReal[i] = 0.0;
    vImag[i] = 0.0;
  }
 
  ArduinoFFT<double> fft(vReal, vImag, FFT_SAMPLE_COUNT, (double)sampleRate);
  fft.windowing(FFTWindow::Hann, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();  // magnitudes now in vReal[0..FFT_SAMPLE_COUNT/2-1]

  // Samples were already scaled by fullScale before the FFT, so band levels are
  // normalized only by the FFT gain. A full-scale tone produces a peak-bin
  // magnitude of ~(N/2) * window coherent gain (Hann ≈ 0.5), which this maps to
  // ~0 dBFS — putting the bands on the same scale as the broadband RMS above.
  const double HANN_COHERENT_GAIN = 0.5;
  const double normScale = (FFT_SAMPLE_COUNT / 2.0) * HANN_COHERENT_GAIN;

  out.sub_bass_dbfs = bandEnergyDbfs(vReal, FFT_SAMPLE_COUNT, sampleRate,   50,  150, normScale);
  out.hum_dbfs      = bandEnergyDbfs(vReal, FFT_SAMPLE_COUNT, sampleRate,  150,  300, normScale);
  out.piping_dbfs   = bandEnergyDbfs(vReal, FFT_SAMPLE_COUNT, sampleRate,  300,  550, normScale);
  out.stress_dbfs   = bandEnergyDbfs(vReal, FFT_SAMPLE_COUNT, sampleRate,  550, 1500, normScale);
  out.high_dbfs     = bandEnergyDbfs(vReal, FFT_SAMPLE_COUNT, sampleRate, 1500, 3000, normScale);
 
  free(vReal);
  free(vImag);
}
 

// ---------------------------------------------------------------------------
// initMicsI2s: configure I2S port for the two INMP441 microphones.
// Returns true on success. Safe to call multiple times (no-op if already up).
// ---------------------------------------------------------------------------
bool initMicsI2s() {
  if (micsI2sInstalled) return true;

  // 1) Create an RX-only channel on the configured port, master role.
  //    dma_frame_num * slot_num * (bits/8) must stay <= 4092 bytes, so 256
  //    frames * 2 slots * 4 bytes = 2048 is safe. The capture loop reads in
  //    512-frame chunks; i2s_channel_read() spans DMA descriptors as needed, so
  //    the DMA buffer size does not have to match the read size.
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(INMP441_I2S_PORT, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num  = 4;    // was dma_buf_count
  chanCfg.dma_frame_num = 256;  // was dma_buf_len
  chanCfg.auto_clear    = false;

  esp_err_t err = i2s_new_channel(&chanCfg, nullptr, &micsRxChan);
  if (err != ESP_OK) {
    Serial.printf("[INMP441] i2s_new_channel failed: %d\n", (int)err);
    micsRxChan = nullptr;
    return false;
  }

  // 2) Standard (Philips) mode, 32-bit slots, stereo so we capture both mics
  //    (one wired L, the other wired R). We still read 32-bit words and shift
  //    right by 8 to recover the INMP441's 24-bit signed sample.
  i2s_std_config_t stdCfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(INMP441_SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)INMP441_BCLK_PIN,
      .ws   = (gpio_num_t)INMP441_WS_PIN,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)INMP441_SD_PIN,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };

  err = i2s_channel_init_std_mode(micsRxChan, &stdCfg);
  if (err != ESP_OK) {
    Serial.printf("[INMP441] i2s_channel_init_std_mode failed: %d\n", (int)err);
    i2s_del_channel(micsRxChan);
    micsRxChan = nullptr;
    return false;
  }

  // 3) Enable the channel so the DMA engine starts clocking in samples.
  err = i2s_channel_enable(micsRxChan);
  if (err != ESP_OK) {
    Serial.printf("[INMP441] i2s_channel_enable failed: %d\n", (int)err);
    i2s_del_channel(micsRxChan);
    micsRxChan = nullptr;
    return false;
  }

  micsI2sInstalled = true;
  Serial.printf("[INMP441] I2S installed: BCLK=%d WS=%d SD=%d rate=%d\n",
                INMP441_BCLK_PIN, INMP441_WS_PIN, INMP441_SD_PIN, INMP441_SAMPLE_RATE);
  return true;
}

void shutdownMicsI2s() {
  if (!micsI2sInstalled) return;
  // The new driver requires disabling the channel before deleting it.
  i2s_channel_disable(micsRxChan);
  i2s_del_channel(micsRxChan);
  micsRxChan = nullptr;
  micsI2sInstalled = false;
  Serial.println("[INMP441] I2S uninstalled");
}

MicMeasurement readMicSamples() {
  MicMeasurement result;
 
  if (!initMicsI2s()) {
    Serial.println("[INMP441] I2S init failed; skipping mic measurement");
    return result;
  }
 
  // Settling: discard the first 256 frames so the mic's DC blocker stabilises.
  // Heap-allocated (512 * 4 = 2 KB) to avoid eating into the loopTask stack.
  const size_t WARMUP_FRAMES = 256;
  {
    int32_t* warmup = (int32_t*)malloc(WARMUP_FRAMES * 2 * sizeof(int32_t));
    if (warmup) {
      size_t warmupBytesRead = 0;
      // i2s_channel_read() takes its timeout directly in milliseconds (the
      // legacy i2s_read() wanted FreeRTOS ticks via pdMS_TO_TICKS).
      i2s_channel_read(micsRxChan, warmup, WARMUP_FRAMES * 2 * sizeof(int32_t), &warmupBytesRead, 500);
      free(warmup);
    } else {
      Serial.println("[INMP441] warmup alloc failed; continuing without settling");
    }
  }
 
  // ── RMS / Peak pass ──────────────────────────────────────────────────────
  // We store every sample so we can reuse the same data for the FFT pass,
  // avoiding a second I2S capture.  The buffer holds FFT_SAMPLE_COUNT frames
  // per channel; anything beyond that still updates RMS/peak but is not fed
  // into the FFT (we have plenty of resolution with 4096 samples at 16 kHz).
  //
  // Memory: 4096 * 2 channels * 4 bytes = 32 kB — fits in heap.
  int32_t* leftBuf  = (int32_t*)malloc(FFT_SAMPLE_COUNT * sizeof(int32_t));
  int32_t* rightBuf = (int32_t*)malloc(FFT_SAMPLE_COUNT * sizeof(int32_t));
  if (!leftBuf || !rightBuf) {
    free(leftBuf); free(rightBuf);
    Serial.println("[INMP441] FFT buffer alloc failed; falling back to RMS-only");
    // Fall back: run the original RMS-only loop (no FFT data will be set)
    leftBuf = rightBuf = nullptr;
  }
 
  // chunk is 512 frames * 2 channels * 4 bytes = 4 KB — moved to heap to
  // prevent the "Stack canary watchpoint triggered (loopTask)" crash.
  const size_t CHUNK_FRAMES = 512;
  int32_t* chunk = (int32_t*)malloc(CHUNK_FRAMES * 2 * sizeof(int32_t));
  if (!chunk) {
    Serial.println("[INMP441] chunk alloc failed; skipping mic measurement");
    free(leftBuf);
    free(rightBuf);
    return result;
  }
 
  double leftSum = 0.0, rightSum = 0.0;       // running sums for DC/mean removal
  double leftSumSq = 0.0, rightSumSq = 0.0;
  int32_t leftPeak = 0, rightPeak = 0;
  uint32_t leftCount = 0, rightCount = 0;
  size_t leftFftCount = 0, rightFftCount = 0;
 
  uint32_t framesRemaining = INMP441_SAMPLE_FRAMES;
 
  while (framesRemaining > 0) {
    size_t framesThisRound = framesRemaining > CHUNK_FRAMES ? CHUNK_FRAMES : framesRemaining;
    size_t bytesWanted = framesThisRound * 2 * sizeof(int32_t);
    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(micsRxChan, chunk, bytesWanted, &bytesRead, 1000);
    if (err != ESP_OK || bytesRead == 0) break;
 
    size_t framesRead = bytesRead / (2 * sizeof(int32_t));
    for (size_t i = 0; i < framesRead; i++) {
      // The new standard (Philips) driver delivers the left channel first:
      // index 0 = left, 1 = right. (The old legacy driver swapped these and
      // gave right first, which is why this mapping is flipped versus before —
      // the physical L/R wiring is unchanged, only the driver's slot order is.)
      int32_t ls = chunk[i * 2 + 0] >> 8;  // 24-bit signed
      int32_t rs = chunk[i * 2 + 1] >> 8;
 
      double rf = (double)rs, lf = (double)ls;
      rightSum   += rf;
      leftSum    += lf;
      rightSumSq += rf * rf;
      leftSumSq  += lf * lf;
 
      int32_t absR = rs < 0 ? -rs : rs;
      int32_t absL = ls < 0 ? -ls : ls;
      if (absR > rightPeak) rightPeak = absR;
      if (absL > leftPeak)  leftPeak  = absL;
 
      rightCount++;
      leftCount++;
 
      // Store into FFT buffers while there is space
      if (leftBuf  && leftFftCount  < FFT_SAMPLE_COUNT) leftBuf[leftFftCount++]   = ls;
      if (rightBuf && rightFftCount < FFT_SAMPLE_COUNT) rightBuf[rightFftCount++] = rs;
    }
    framesRemaining -= framesRead;
    if (framesRead == 0) break;
  }
 
  free(chunk);  // release before FFT heap allocations in computeBands()
 
  // ── Fill RMS / Peak stats ─────────────────────────────────────────────────
  const double FULL_SCALE = 8388608.0; // 2^23
 
  auto fillStats = [&](MicChannelStats& s, double sum, double sumSq,
                       int32_t peak, uint32_t count) {
    if (count == 0) return;
    s.ok = true;
    s.sampleCount = count;
    // Remove the INMP441 DC offset before computing RMS: the AC power is the
    // variance, i.e. mean-of-squares minus square-of-mean. Without this the
    // mic's large DC bias dominates and pins the RMS near full scale.
    double mean     = sum / (double)count;
    double variance = sumSq / (double)count - mean * mean;
    if (variance < 0.0) variance = 0.0;   // guard against rounding below zero
    double rms = sqrt(variance);
    s.rmsNormalized = (float)(rms / FULL_SCALE);
    s.rmsDbfs  = s.rmsNormalized > 0.0f
                 ? (float)(20.0 * log10((double)s.rmsNormalized))
                 : -200.0f;
    s.peakDbfs = peak > 0
                 ? (float)(20.0 * log10((double)peak / FULL_SCALE))
                 : -200.0f;
  };

  fillStats(result.left,  leftSum,  leftSumSq,  leftPeak,  leftCount);
  fillStats(result.right, rightSum, rightSumSq, rightPeak, rightCount);
  result.ok = result.left.ok || result.right.ok;
 
  // ── FFT band analysis ──────────────────────────────────────────────────────
  // Free each buffer immediately after its FFT pass so the next pass only holds
  // one 16 kB buffer instead of two while computeBands allocates vReal/vImag.
  if (leftBuf && leftFftCount >= 64) {
    computeBands(leftBuf,  leftFftCount,  INMP441_SAMPLE_RATE, FULL_SCALE, result.left.bands);
  }
  free(leftBuf);
  leftBuf = nullptr;
  if (rightBuf && rightFftCount >= 64) {
    computeBands(rightBuf, rightFftCount, INMP441_SAMPLE_RATE, FULL_SCALE, result.right.bands);
  }
  free(rightBuf);
  rightBuf = nullptr;
 
  Serial.printf("[INMP441] L: rms=%.1f dBFS peak=%.1f dBFS | sub=%.1f hum=%.1f pipe=%.1f stress=%.1f hi=%.1f\n",
    result.left.rmsDbfs, result.left.peakDbfs,
    result.left.bands.sub_bass_dbfs, result.left.bands.hum_dbfs,
    result.left.bands.piping_dbfs,   result.left.bands.stress_dbfs,
    result.left.bands.high_dbfs);
  Serial.printf("[INMP441] R: rms=%.1f dBFS peak=%.1f dBFS | sub=%.1f hum=%.1f pipe=%.1f stress=%.1f hi=%.1f\n",
    result.right.rmsDbfs, result.right.peakDbfs,
    result.right.bands.sub_bass_dbfs, result.right.bands.hum_dbfs,
    result.right.bands.piping_dbfs,   result.right.bands.stress_dbfs,
    result.right.bands.high_dbfs);
 
  return result;
}

#endif // ENABLE_INMP441_MICS
