// ────────────────────────────────────────────────────────────
// CYBER CLOCK — WiFi Setup UI (no LVGL, raw IDF API + touch)
// ────────────────────────────────────────────────────────────
#pragma once
#include <M5Unified.h>
#include <Preferences.h>
#include <time.h>

void runWifiSetup();
void reconnectWifi();   // re-run wizard without re-init hosted stack
bool autoConnectAndSync();
void updateClockFromNTP(int& h, int& m);
