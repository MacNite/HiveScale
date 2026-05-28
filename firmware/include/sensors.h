// sensors.h — time keeping, load-cell reads and assembly of the per-cycle
// measurement JSON payload (the heart of each upload cycle).
#pragma once

#include <Arduino.h>
#include <HX711.h>

String timestampNow();
void syncTime();
void initializeTime(bool wokeFromDeepSleep);

long readAverageRaw(HX711& scale, int samples = 15);
float weightFromRaw(long raw, long offset, float factor);

String createMeasurementJson();
