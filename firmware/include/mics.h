// mics.h — dual INMP441 I2S microphone capture and per-band FFT analysis.
// The entire feature is compiled out unless ENABLE_INMP441_MICS is set.
#pragma once

#include <Arduino.h>
#include "config.h"

#if ENABLE_INMP441_MICS
#include <driver/i2s.h>

// Number of samples fed into the FFT. Must be a power of two and fit in RAM.
// 2048 samples: vReal + vImag = 2 × 16 kB = 32 kB per computeBands() call.
// At 16 kHz this gives ~7.8 Hz resolution — more than enough to resolve all
// five bands (narrowest is piping at 300–550 Hz = 32 bins).
#define FFT_SAMPLE_COUNT 2048

struct MicBands {
  float sub_bass_dbfs = NAN;  //   50 - 150 Hz  structural / low rumble
  float hum_dbfs      = NAN;  //  150 - 300 Hz  normal colony hum
  float piping_dbfs   = NAN;  //  300 - 550 Hz  queen piping / tooting (pre-swarm)
  float stress_dbfs   = NAN;  //  550 - 1500 Hz agitated / robbing colony
  float high_dbfs     = NAN;  // 1500 - 3000 Hz harmonic overtones
};

struct MicChannelStats {
  bool ok = false;
  float rmsDbfs        = NAN;  // Broadband RMS in dBFS
  float peakDbfs       = NAN;  // Peak in dBFS
  float rmsNormalized  = NAN;  // Linear RMS fraction of full scale (0..1)
  uint32_t sampleCount = 0;
  MicBands bands;              // Per-band energy in dBFS
};

struct MicMeasurement {
  bool ok = false;
  MicChannelStats left;
  MicChannelStats right;
};

bool initMicsI2s();
void shutdownMicsI2s();
MicMeasurement readMicSamples();

#endif // ENABLE_INMP441_MICS
