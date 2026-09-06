// portal.h — WiFi provisioning / captive setup portal, the physical setup
// button handler, and calibration-mode control. The portal also exposes the
// SD TAR export (implemented in storage_power).
#pragma once

#include <Arduino.h>
#include "config.h"

// ---- Calibration mode -----------------------------------------------------
bool calibrationModeExpired();
void stopCalibrationMode(const String& reason);
void startCalibrationMode(unsigned long intervalSeconds, unsigned long timeoutSeconds);

// ---- HTML / portal helpers ------------------------------------------------
String htmlEscape(String s);
IPAddress provisioningPortalIp();
String provisioningPortalUrl();
void sendNoCacheHeaders();
void sendPortalRedirect();
void handleCaptivePortalProbe();
void appendLastSensorPanel(String& html);

// ---- HTTP route handlers --------------------------------------------------
void handleSdDownloadAll();
#if ENABLE_BLE_SCAN
void handleBleScan();
#endif
void handleSetupRoot();
void handleSetupSave();
void handleSetupReset();

// ---- Portal lifecycle + button -------------------------------------------
void startProvisioningPortal();
void stopProvisioningPortal();
void handleButton();

// Ask for the portal without opening it here. A `start_provisioning` command is
// handled in the middle of an upload cycle, where tearing WiFi down for the AP
// would strand the rest of that cycle (cached-line upload, OTA check) — so the
// command only sets the request and the cycle opens the AP once it is done,
// which is exactly what a button press does.
void requestProvisioningPortal();
// Open the portal if one was requested; a no-op otherwise. Called right after
// each upload cycle.
//
// May not return: when a BLE discovery scan can no longer run in this boot (the
// normal state once a paired sensor has been measured — see ble_stack.h), the
// request is parked in RTC memory and the hub reboots, so the portal opens in a
// port lifetime the scan is actually allowed to use. Without that, the portal
// pairing dropdowns come up empty on exactly the hubs that have a sensor.
void startRequestedProvisioningPortal();

// True when this boot exists to open the provisioning portal, because
// startRequestedProvisioningPortal() rebooted for a clean BLE scan. Consumes
// the request, so a portal that times out is followed by ordinary cycles.
bool consumePortalBootRequest();

// Read the setup button from inside a running upload cycle, latching a portal
// request if it is held. With deep sleep on, loop()/handleButton() never run,
// and on the XIAO ESP32-C6 the button cannot wake the hub at all (GPIO9 is not
// an LP-IO pad), so the awake window is the only place a press can be seen.
void pollSetupButton();

// Record that a setup-button press has already been acted on, so the same
// unbroken hold cannot ALSO trigger handleButton()'s ten-second factory reset.
// Called wherever a held button has just opened (or queued) the portal.
void markSetupButtonHandled();
