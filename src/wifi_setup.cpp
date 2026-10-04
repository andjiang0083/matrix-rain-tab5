// ────────────────────────────────────────────────────────────
// CYBER CLOCK — WiFi Setup UI (ESP32-Hosted SDIO + IDF API)
// ────────────────────────────────────────────────────────────
#include "wifi_setup.h"
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <esp32-hal-hosted.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "matrix_gui.h"
#include "ui_kit.h"

// ── Tab5 Hosted SDIO pins ──
static const int H_CLK = 12, H_CMD = 13, H_D0 = 11, H_D1 = 10, H_D2 = 9, H_D3 = 8, H_RST = 15;

// ── FSM states ──
enum SetupState {
  ST_INIT, ST_WIFI_SCAN, ST_WIFI_LIST, ST_KEYBOARD,
  ST_CONNECTING, ST_TZ_SELECT, ST_CONFIRM, ST_DONE,
};

// ── Global UI state ──
static char     g_ssid[64];
static char     g_pass[128];
static int32_t  g_tzOffset   = 28800;
static int      g_passLen    = 0;
static bool     g_shift      = false;
static bool     g_alphaMode  = true;
static int      g_selNet     = -1;
static int      g_scanCount  = 0;
static String   g_scanSSIDs[32];
static int32_t  g_scanRSSI[32];
static bool     g_scanOpen[32];
static int      g_tzIdx      = 0;
static bool     g_kbFull     = true;   // force full keyboard redraw
static bool     g_passDirty  = false;  // just password field update
static bool     g_tzDirty    = true;   // timezone needs redraw
static uint32_t g_lastRedraw = 0;

// ────────────────────────────────────────────────────────────
// Time source — never display a time we cannot justify.
//
// NTP is the only authoritative source. The RX8130CE (holding UTC) is a
// fallback for boots without a network. The rule that matters:
//
//   getLocalTime() is NOT an "NTP has synced" signal. It returns true as soon
//   as the system clock merely looks plausible — and M5.begin() seeds that
//   clock from the hardware RTC (RTC_Class::setSystemTimeFromRtc()), so right
//   after configTime() it returns true immediately with the *pre-sync* time.
//   Waiting on it therefore raced the NTP response: a stale RTC value was
//   confirmed on screen and written back into the RTC, which is why the clock
//   still showed it after a "successful" sync.
//
// The SNTP notification callback / sync status is the real signal. Everything
// downstream then reads the system clock — the RTC is only ever a seed.
// ────────────────────────────────────────────────────────────
static constexpr time_t EPOCH_2025 = 1735689600;   // 2025-01-01T00:00:00Z
static volatile bool g_sntpSynced  = false;
static bool g_timeTrusted          = false;        // NTP confirmed, or RTC validated
static bool g_rtcSeedTried         = false;        // one fallback attempt per boot

static void onSntpSync(struct timeval*) { g_sntpSynced = true; }

// Start (or restart) SNTP and wait for a real sync. False on timeout.
static bool ntpSync(uint32_t gmtOffsetSec, uint32_t timeoutMs) {
  // Association is not reachability. Asking for the time before DHCP hands out
  // a lease means the first query goes out with no DNS and is silently lost,
  // which reads on the panel as "NTP is broken" (it cost us a whole boot).
  {
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    uint32_t tw = millis();
    while (millis() - tw < 8000) {
      esp_netif_ip_info_t ip = {};
      if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr != 0) break;
      esp_task_wdt_reset();
      delay(100);
    }
  }

  g_sntpSynced = false;
  configTime((long)gmtOffsetSec, 0, "pool.ntp.org", "time.google.com");
  sntp_set_time_sync_notification_cb(onSntpSync);   // after configTime: it starts SNTP
  uint32_t t0 = millis();
  bool rearmed = false;
  while (millis() - t0 < timeoutMs) {
    esp_task_wdt_reset();
    if (g_sntpSynced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      g_timeTrusted = true;
      return true;
    }
    // One unacknowledged query is all lwIP sends before it backs off, so if that
    // one was lost, re-arm once rather than waiting out the whole window.
    if (!rearmed && millis() - t0 > timeoutMs / 2) {
      rearmed = true;
      sntp_restart();
    }
    delay(100);
  }
  return false;
}

// Days-from-civil → UTC epoch, in pure integer math (no mktime/timegm, no
// dependency on the ambient TZ). Verified against Python's calendar.
static time_t utcEpoch(int year, int month, int day, int h, int mi, int s) {
  int y = year - (month <= 2 ? 1 : 0);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (unsigned)(month > 2 ? month - 3 : month + 9) + 2) / 5
               + (unsigned)day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long long days = (long long)era * 146097 + (long long)doe - 719468;
  return (time_t)(days * 86400LL + h * 3600 + mi * 60 + s);
}

// Timezone offset cache. The clock re-derives HH:MM every few seconds, and on
// this board flash and PSRAM share the MSPI path — the DSI scan-out is already
// marginal, so the clock path must not touch NVS. Read once, then keep it in
// RAM (the wizard and the boot path refresh it when the user changes it).
static int32_t g_tzCache = INT32_MIN;      // seconds east of UTC; INT32_MIN = not loaded

static int32_t tzSeconds() {
  if (g_tzCache == INT32_MIN) {
    Preferences prefs;
    prefs.begin("cyberclock", true);
    g_tzCache = prefs.getInt("tz", 28800);
    prefs.end();
  }
  return g_tzCache;
}

// Read the RTC and reject anything a broken / never-set RTC could return.
static bool rtcReadUTC(m5::rtc_datetime_t& out) {
  m5::rtc_datetime_t r;
  if (!M5.Rtc.isEnabled() || !M5.Rtc.getDateTime(&r)) return false;  // driver checks BCD + weekday
  if (r.date.year < 2025 || r.date.year > 2099) return false;
  if (r.date.month < 1 || r.date.month > 12) return false;
  if (r.date.date  < 1 || r.date.date  > 31) return false;
  if (r.time.hours   < 0 || r.time.hours   > 23) return false;
  if (r.time.minutes < 0 || r.time.minutes > 59) return false;
  if (r.time.seconds < 0 || r.time.seconds > 60) return false;
  out = r;
  return true;
}

// Write UTC into the RTC and verify it landed: M5.Rtc.setDateTime() returns
// void, so a write that never took effect is otherwise indistinguishable from
// a good one (this is how a stale RTC kept surviving the sync).
static bool rtcWriteUTC(const struct tm& utcTm) {
  if (!M5.Rtc.isEnabled()) return false;
  m5::rtc_datetime_t w;
  w.date.year    = utcTm.tm_year + 1900;
  w.date.month   = utcTm.tm_mon + 1;
  w.date.date    = utcTm.tm_mday;
  w.date.weekDay = utcTm.tm_wday;
  w.time.hours   = utcTm.tm_hour;
  w.time.minutes = utcTm.tm_min;
  w.time.seconds = utcTm.tm_sec;
  M5.Rtc.setDateTime(w);

  m5::rtc_datetime_t r;
  if (!rtcReadUTC(r)) return false;
  if (r.date.year != w.date.year || r.date.month != w.date.month
      || r.date.date != w.date.date) return false;
  int dw = (r.time.hours * 3600 + r.time.minutes * 60 + r.time.seconds)
         - (w.time.hours * 3600 + w.time.minutes * 60 + w.time.seconds);
  return dw >= -2 && dw <= 2;        // the seconds register may tick across the write
}

// Last resort: seed the system clock from the RTC. The RTC must be plausible
// *and* actually ticking, so a stopped or garbage RTC cannot get through.
static bool rtcSeedSystemTime() {
  m5::rtc_datetime_t a, b;
  if (!rtcReadUTC(a)) return false;
  delay(1100);
  if (!rtcReadUTC(b)) return false;
  int ta = a.time.hours * 3600 + a.time.minutes * 60 + a.time.seconds;
  int tb = b.time.hours * 3600 + b.time.minutes * 60 + b.time.seconds;
  int step = tb - ta; if (step < 0) step += 86400;
  if (step < 1 || step > 3) return false;            // not ticking, or jumped

  time_t epoch = utcEpoch(b.date.year, b.date.month, b.date.date,
                          b.time.hours, b.time.minutes, b.time.seconds);
  if (epoch < EPOCH_2025) return false;
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
  return true;
}

// ── Helpers ──
static inline bool hitR(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

// Chrome comes from the kit: one title bar (step id right-aligned), one back
// control below the bar — the single place the user picked.
static void drawTitle(const char* s) { UI::bar(M5.Display, s); }

// Title with step indicator (e.g. "Select Network  1/4")
static void drawStepTitle(const char* step, const char* s) { UI::bar(M5.Display, s, step); }

static bool getTouch(int& tx, int& ty) {
  M5.update();
  auto cnt = M5.Touch.getCount();
  if (cnt == 0) return false;
  auto t = M5.Touch.getDetail();
  tx = t.x; ty = t.y;
  return true;
}

static void waitRelease() {
  uint32_t t = millis();
  while (millis() - t < 3000) {      // 3s safety timeout
    M5.update();
    if (M5.Touch.getCount() == 0) { delay(10); return; }
    delay(5);
  }
}

// ── Timezone list ──
static const struct { const char* label; int32_t offset; } TZ_LIST[] = {
  { "UTC-12", -43200 }, { "UTC-11", -39600 }, { "UTC-10", -36000 },
  { "UTC-9",  -32400 }, { "UTC-8",  -28800 }, { "UTC-7",  -25200 },
  { "UTC-6",  -21600 }, { "UTC-5",  -18000 }, { "UTC-4",  -14400 },
  { "UTC-3",  -10800 }, { "UTC-2",  -7200  }, { "UTC-1",  -3600  },
  { "UTC0",   0       }, { "UTC+1",  3600   }, { "UTC+2",  7200   },
  { "UTC+3",  10800   }, { "UTC+4",  14400  }, { "UTC+5",  18000  },
  { "UTC+5:30",19800  }, { "UTC+6",  21600  }, { "UTC+7",  25200  },
  { "UTC+8",  28800   }, { "UTC+9",  32400  }, { "UTC+10", 36000  },
  { "UTC+11", 39600   }, { "UTC+12", 43200  }, { "UTC+13", 46800  },
  { "UTC+14", 50400   },
};
static const int TZ_COUNT = sizeof(TZ_LIST)/sizeof(TZ_LIST[0]);

// ── Screen: WiFi list ──
static void drawListScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("1/4", "Select Network");
  // No back row here: the list needs the full height for 12 rows, so Cancel
  // lives in the bottom action row instead.

  for (int i = 0; i < g_scanCount && i < UI::LIST_MAX_ROWS; i++) {
    int yy = UI::rowY(i);
    bool sel = (i == g_selNet);
    UI::rowBg(d, yy, sel);

    // SSID — sanitize non-ASCII → draw □ boxes
    char clean[65]; int replPos[16];
    const String& raw = g_scanSSIDs[i];
    int rc = sanitizeSSID(clean, 64, raw.c_str(), raw.length(), replPos, 16);
    UI::left(d, UI::T_BODY, clean, UI::LIST_X + 36,
             yy + (UI::ROW_H - UI::cellH(UI::T_BODY)) / 2,
             sel ? MG::BRIGHT : MG::BODY);

    // Draw □ over non-ASCII replacement positions
    for (int r = 0; r < rc; r++) {
      char before[65]; strncpy(before, clean, replPos[r]); before[replPos[r]] = 0;
      UI::font(d, UI::T_BODY);
      int16_t bx = UI::LIST_X + 36 + d.textWidth(before);
      d.drawRect(bx, yy + 10, 14, 20, d.color565(0, MG::REPLACE, 0));
    }

    // Signal bars — 5 steps, bottom-aligned in the row
    int bars = constrain(map(g_scanRSSI[i], -90, -30, 1, 5), 1, 5);
    for (int b = 0; b < 5; b++) {
      int hgt = b * 5 + 6;
      uint16_t c = (b < bars) ? d.color565(0, map(b, 0, 4, MG::DIM, MG::BRIGHT), 0)
                              : d.color565(0, MG::LINE, 0);
      d.fillRect(1080 + b * 14, yy + UI::ROW_H - 8 - hgt, 10, hgt, c);
    }

    // OPEN tag
    if (g_scanOpen[i])
      UI::right(d, UI::T_SMALL, "OPEN", UI::LIST_X + UI::LIST_W - 24,
                yy + (UI::ROW_H - UI::cellH(UI::T_SMALL)) / 2, MG::DIM);
  }

  UI::button(d, UI::ACT_X,  UI::ACT_Y, UI::ACT_W, UI::ACT_H, "Rescan", UI::SECONDARY);
  UI::button(d, UI::ACT_XR, UI::ACT_Y, UI::ACT_W, UI::ACT_H, "Cancel", UI::GHOST);
}

static void handleListScreen(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (hitR(tx, ty, UI::ACT_XR, UI::ACT_Y, UI::ACT_W, UI::ACT_H)) { state = ST_DONE; waitRelease(); return; }
  if (hitR(tx, ty, UI::ACT_X, UI::ACT_Y, UI::ACT_W, UI::ACT_H)) { state = ST_WIFI_SCAN; waitRelease(); return; }
  if (ty < UI::LIST_TOP) return;
  int idx = (ty - UI::LIST_TOP) / UI::ROW_PITCH;
  if (idx >= 0 && idx < g_scanCount && idx < UI::LIST_MAX_ROWS) {
    g_selNet = idx;
    strncpy(g_ssid, g_scanSSIDs[idx].c_str(), sizeof(g_ssid)-1);
    g_ssid[sizeof(g_ssid)-1] = 0;
    if (g_scanOpen[idx]) { g_pass[0] = 0; g_passLen = 0; state = ST_CONNECTING; }
    else { g_pass[0] = 0; g_passLen = 0; g_shift = false; g_kbFull = true; state = ST_KEYBOARD; }
    waitRelease();
  }
}

// ── Keyboard ──
static const int K_SZ = 108, K_GAP = 6;
static const int K_BASE_Y = 216;                       // keys start below title + field
static const int K_FIELD_X = 40, K_FIELD_Y = 140, K_FIELD_W = 1200, K_FIELD_H = 64;
static const int K_TEXT_X = K_FIELD_X + 28;
static const int K_TEXT_Y = K_FIELD_Y + (K_FIELD_H - 24) / 2;
static const char* K_ALPHA[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const int K_ALPHA_LEN[] = { 10, 9, 7 };
static const char* K_NUM[] = { "1234567890", "-_@#.$%+=", ",/:;\"'!?()" };
static const int K_NUM_LEN[] = { 10, 10, 10 };

// One source of truth for the bottom key bar: the draw code and the hit test
// both call this, so a tap can never land on the wrong key after a layout tweak.
struct KeyBar { int y, shiftX, modeX, spaceX, spaceW, delX, connX, connW; };
static KeyBar keyBar() {
  KeyBar k{};
  k.y = K_BASE_Y + 3 * (K_SZ + K_GAP);
  int lx = K_FIELD_X;
  k.shiftX = lx; lx += 106;                            // 100 wide + 6 gap
  k.modeX  = lx; lx += 106;
  k.connW  = 240; k.connX = 1280 - K_FIELD_X - k.connW;
  k.delX   = k.connX - 6 - 100;
  k.spaceX = lx; k.spaceW = k.delX - 6 - lx;
  return k;
}

// Same idea for the three letter/number rows.
static void keyRect(int r, int k, int& x, int& y) {
  const int* lens = g_alphaMode ? K_ALPHA_LEN : K_NUM_LEN;
  y = K_BASE_Y + r * (K_SZ + K_GAP);
  int totalW = lens[r] * K_SZ + (lens[r] - 1) * K_GAP;
  x = (1280 - totalW) / 2 + k * (K_SZ + K_GAP);
}

static void drawPasswordOnly();   // defined below; the full keyboard redraw reuses it

static void drawKeyboardScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("2/4", "Enter Password");
  UI::back(d, "< CANCEL");
  drawPasswordOnly();

  // Letter / number keys
  const char** rows = g_alphaMode ? K_ALPHA : K_NUM;
  const int* lens   = g_alphaMode ? K_ALPHA_LEN : K_NUM_LEN;
  for (int r = 0; r < 3; r++) {
    for (int k = 0; k < lens[r]; k++) {
      int xx, yy; keyRect(r, k, xx, yy);
      char lbl[2] = { rows[r][k], 0 };
      UI::button(d, xx, yy, K_SZ, K_SZ, lbl, UI::KEY, UI::T_BODY);
    }
  }

  // Bottom row
  KeyBar kb = keyBar();
  UI::button(d, kb.shiftX, kb.y, 100, K_SZ, "SHFT", g_shift ? UI::PRIMARY : UI::SECONDARY, UI::T_BODY);
  UI::button(d, kb.modeX,  kb.y, 100, K_SZ, g_alphaMode ? "123" : "ABC", UI::SECONDARY, UI::T_BODY);
  UI::button(d, kb.spaceX, kb.y, kb.spaceW, K_SZ, "SPACE", UI::SECONDARY, UI::T_BODY);
  UI::button(d, kb.delX,   kb.y, 100, K_SZ, "DEL", UI::SECONDARY, UI::T_BODY);
  UI::button(d, kb.connX,  kb.y, kb.connW, K_SZ, "Connect",
             g_passLen > 0 ? UI::PRIMARY : UI::GHOST, UI::T_BODY);
}

// ── Draw only the password field (fast, no flicker) ──
static void drawPasswordOnly() {
  auto& d = M5.Display;
  d.fillRoundRect(K_FIELD_X, K_FIELD_Y, K_FIELD_W, K_FIELD_H, 8, d.color565(0, MG::FIELD, 0));
  d.drawRoundRect(K_FIELD_X, K_FIELD_Y, K_FIELD_W, K_FIELD_H, 8, d.color565(0, MG::LINE, 0));
  UI::font(d, UI::T_BODY);
  if (g_passLen == 0) {
    char ph[80];
    snprintf(ph, sizeof(ph), "password for %s", g_ssid);
    UI::left(d, UI::T_BODY, ph, K_TEXT_X, K_TEXT_Y, MG::DIM);
  } else {
    // masked: never leave the network password legible on the panel
    char buf[65]; int len = g_passLen < 64 ? g_passLen : 64;
    for (int i = 0; i < len; i++) buf[i] = '*';
    buf[len] = 0;
    if (d.textWidth(buf) > K_FIELD_W - 56) {
      int show = 50; if (show > len) show = len;
      buf[show] = 0;                       // keep the newest characters visible
    }
    UI::left(d, UI::T_BODY, buf, K_TEXT_X, K_TEXT_Y, MG::BODY);
  }
}

static void handleKeyboard(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (UI::backHit(tx, ty)) { state = ST_WIFI_LIST; waitRelease(); return; }
  KeyBar kb = keyBar();
  // Shift  → full redraw (the key itself changes state)
  if (hitR(tx, ty, kb.shiftX, kb.y, 100, K_SZ)) { g_shift = !g_shift; g_kbFull = true; waitRelease(); return; }
  // 123 toggle → full redraw
  if (hitR(tx, ty, kb.modeX, kb.y, 100, K_SZ)) { g_alphaMode = !g_alphaMode; g_shift = false; g_kbFull = true; waitRelease(); return; }
  // Space → password only
  if (hitR(tx, ty, kb.spaceX, kb.y, kb.spaceW, K_SZ)) {
    if (g_passLen < 120) { g_pass[g_passLen++] = ' '; g_pass[g_passLen] = 0; g_passDirty = true; }
    waitRelease(); return;
  }
  // Backspace → password only
  if (hitR(tx, ty, kb.delX, kb.y, 100, K_SZ)) {
    if (g_passLen > 0) { g_pass[--g_passLen] = 0; g_passDirty = true; }
    waitRelease(); return;
  }
  // Connect → transition
  if (hitR(tx, ty, kb.connX, kb.y, kb.connW, K_SZ)) {
    if (g_passLen > 0) { g_pass[g_passLen] = 0; state = ST_CONNECTING; }
    waitRelease(); return;
  }
  // Letter/number keys → password only
  for (int r = 0; r < 3; r++) {
    const int* lens = g_alphaMode ? K_ALPHA_LEN : K_NUM_LEN;
    for (int k = 0; k < lens[r]; k++) {
      int xx, yy; keyRect(r, k, xx, yy);
      if (hitR(tx, ty, xx, yy, K_SZ, K_SZ)) {
        char ch = g_alphaMode ? K_ALPHA[r][k] : K_NUM[r][k];
        if (g_passLen < 120) { g_pass[g_passLen++] = ch; g_pass[g_passLen] = 0; }
        bool wasShift = g_shift;
        if (g_alphaMode && g_shift) g_shift = false;
        g_passDirty = true;
        if (wasShift) g_kbFull = true;   // repaint SHFT so its state is visible
        waitRelease(); return;
      }
    }
  }
}

// ── Screen: Connecting ──
static void drawConnectingScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("3/4", "Connecting");
  UI::back(d, "< CANCEL");
  UI::centered(d, UI::T_BIG, g_ssid, 240, MG::BODY);
  wifi_ap_record_t ap; bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
  if (connected) {
    UI::centered(d, UI::T_BODY, "Connected!", 310, MG::BRIGHT);
    UI::centered(d, UI::T_BODY, "touch to continue", 356, MG::DIM);
  } else {
    UI::centered(d, UI::T_BODY, "connecting to the access point ...", 310, MG::DIM);
  }
}

static void handleConnecting(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (UI::backHit(tx, ty)) { esp_wifi_disconnect(); state = ST_WIFI_LIST; waitRelease(); return; }
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) { state = ST_TZ_SELECT; g_tzDirty = true; waitRelease(); }
}

// ── Screen: Timezone ──
// Grid geometry is shared by the draw code and the hit test.
static const int TZ_COLS = 6, TZ_BW = 190, TZ_BH = 48, TZ_GAP = 10, TZ_Y0 = 140;
static const int TZ_DONE_X = 1280 - 24 - 240;
static void tzCell(int i, int& x, int& y) {
  int startX = (1280 - (TZ_COLS * TZ_BW + (TZ_COLS - 1) * TZ_GAP)) / 2;
  x = startX + (i % TZ_COLS) * (TZ_BW + TZ_GAP);
  y = TZ_Y0 + (i / TZ_COLS) * (TZ_BH + TZ_GAP);
}

static void drawTzScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("4/4", "Select Timezone");
  UI::back(d, "< CANCEL");
  for (int i = 0; i < TZ_COUNT; i++) {
    int bx, by; tzCell(i, bx, by);
    bool sel = (i == g_tzIdx);
    d.fillRoundRect(bx, by, TZ_BW, TZ_BH, UI::RAD, d.color565(0, sel ? MG::SEL : MG::PANEL, 0));
    d.drawRoundRect(bx, by, TZ_BW, TZ_BH, UI::RAD, d.color565(0, sel ? MG::BTN_BDR : MG::LINE, 0));
    UI::center(d, UI::T_BODY, TZ_LIST[i].label, bx, by, TZ_BW, TZ_BH, sel ? MG::BRIGHT : MG::BODY);
  }
  UI::button(d, TZ_DONE_X, UI::ACT_Y, 240, UI::ACT_H, "Done", UI::PRIMARY);
}

static void handleTz(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  // Done button → confirm screen (not ST_DONE)
  if (hitR(tx, ty, TZ_DONE_X, UI::ACT_Y, 240, UI::ACT_H)) {
    g_tzOffset = TZ_LIST[g_tzIdx].offset; state = ST_CONFIRM; waitRelease(); return;
  }
  if (UI::backHit(tx, ty)) { state = ST_WIFI_LIST; waitRelease(); return; }
  for (int i = 0; i < TZ_COUNT; i++) {
    int bx, by; tzCell(i, bx, by);
    if (hitR(tx, ty, bx, by, TZ_BW, TZ_BH)) {
      g_tzIdx = i; g_tzOffset = TZ_LIST[i].offset; g_tzDirty = true; waitRelease(); return;
    }
  }
}

// ── Time confirmation — ONE screen for the wizard and the boot path ──
// Both call sites used to carry their own copy of this layout, which is exactly
// how the two drifted apart before. Returns true when the user confirms.
static bool confirmTimeScreen(struct tm& t, const char* altLabel) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  UI::bar(d, "Confirm Current Time", "SYNC");

  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  UI::font(d, UI::T_CLOCK);
  const int timeW = d.textWidth("0000-00-00  00:00:00");   // fixed max width
  const int timeH = UI::cellH(UI::T_CLOCK);
  const int timeY = 200;
  const int timeX = (1280 - timeW) / 2;
  d.setTextColor(UI::accent(d));
  d.drawString(buf, timeX, timeY);

  UI::centered(d, UI::T_BODY, "Time synced successfully", 300, MG::BODY);
  UI::hint(d, "TIME SYNCED FROM THE INTERNET", 344);

  const int PW = 240, gap = 20;
  const int okX = 1280 - 24 - PW;
  const int altX = okX - gap - PW;
  UI::button(d, altX, UI::ACT_Y, PW, UI::ACT_H, altLabel, UI::SECONDARY);
  UI::button(d, okX,  UI::ACT_Y, PW, UI::ACT_H, "Confirm",  UI::PRIMARY);

  time_t lastSec = t.tm_sec;
  while (true) {
    M5.update(); esp_task_wdt_reset();

    // Live time update — seconds tick
    time_t now = time(nullptr);
    struct tm cur;
    localtime_r(&now, &cur);
    if (cur.tm_sec != lastSec) {
      lastSec = cur.tm_sec;
      char nb[64];
      snprintf(nb, sizeof(nb), "%04d-%02d-%02d  %02d:%02d:%02d",
        cur.tm_year+1900, cur.tm_mon+1, cur.tm_mday,
        cur.tm_hour, cur.tm_min, cur.tm_sec);
      d.fillRect(timeX, timeY, timeW, timeH, TFT_BLACK);
      UI::font(d, UI::T_CLOCK); d.setTextColor(UI::accent(d));
      d.drawString(nb, timeX, timeY);
    }

    int tx, ty;
    if (getTouch(tx, ty)) {
      if (hitR(tx, ty, okX,  UI::ACT_Y, PW, UI::ACT_H)) { waitRelease(); return true; }
      if (hitR(tx, ty, altX, UI::ACT_Y, PW, UI::ACT_H)) { waitRelease(); return false; }
    }
    delay(20);
  }
}

// ── Screen: Time confirmation (NTP sync + display) ──
static void drawConfirmScreen(int32_t tz, int& state) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  UI::bar(d, "Syncing Time", "SYNC");
  UI::centered(d, UI::T_BIG, "Contacting the time servers ...", 300, MG::BODY);
  
  // NTP sync — wait for a real sync, not for getLocalTime() to look sane.
  struct tm t;
  bool synced = ntpSync((uint32_t)tz, 20000);
  if (synced) getLocalTime(&t);
  
  if (synced && getLocalTime(&t)) {
    // ── Save UTC to hardware RTC (verified — see rtcWriteUTC) ──
    { time_t utcNow = time(nullptr); struct tm utcTm; gmtime_r(&utcNow, &utcTm);
      bool rtcOk = rtcWriteUTC(utcTm);
      Serial.printf("[TIME] ntp=%04d-%02d-%02d %02d:%02d:%02d rtc_write=%s\n",
        t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
        rtcOk ? "ok" : "FAILED"); }

    // Same confirmation screen the boot path shows.
    state = confirmTimeScreen(t, "Reselect") ? ST_DONE : ST_TZ_SELECT;
    g_tzDirty = true;
  } else {
    // NTP failed → offer a retry, in the kit's warning style
    d.fillScreen(TFT_BLACK);
    UI::bar(d, "Time Sync Failed", "SYNC");
    UI::centered(d, UI::T_BIG, "NTP time sync failed", 260, MG::WARN_G);
    UI::hint(d, "check the network, then retry", 320);

    const int bx = 1280 - 24 - 240;
    UI::button(d, bx, UI::ACT_Y, 240, UI::ACT_H, "Retry", UI::WARN);

    while (true) {
      M5.update(); esp_task_wdt_reset();
      int tx, ty;
      if (getTouch(tx, ty) && hitR(tx, ty, bx, UI::ACT_Y, 240, UI::ACT_H)) {
        waitRelease(); state = ST_TZ_SELECT; g_tzDirty = true; return;
      }
      delay(20);
    }
  }
}

// ── Scan using IDF API ──
static int doScan() {
  wifi_scan_config_t sc = {};
  sc.show_hidden = true;
  sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  sc.scan_time.active.min = 100;
  sc.scan_time.active.max = 300;
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;
  uint16_t count = 0;
  if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) return 0;
  if (count > 32) count = 32;
  wifi_ap_record_t recs[32];
  if (esp_wifi_scan_get_ap_records(&count, recs) != ESP_OK) return 0;
  for (int i = 0; i < (int)count; i++) {
    g_scanSSIDs[i] = String((const char*)recs[i].ssid);
    g_scanRSSI[i] = recs[i].rssi;
    g_scanOpen[i] = (recs[i].authmode == WIFI_AUTH_OPEN);
  }
  return count;
}

// ── Connect using IDF API ──
static bool doConnect() {
  wifi_config_t cfg = {};
  strlcpy((char*)cfg.sta.ssid, g_ssid, sizeof(cfg.sta.ssid));
  strlcpy((char*)cfg.sta.password, g_pass, sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  esp_wifi_disconnect();
  delay(100);
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) return false;
  if (esp_wifi_connect() != ESP_OK) return false;
  uint32_t start = millis();
  while (millis() - start < 15000) {
    M5.update(); esp_task_wdt_reset();
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return true;
    delay(50);
  }
  return false;
}

// ── Initialize Hosted SDIO + WiFi ──
static bool initHostedWifi() {
  static bool hostedDone = false;
  if (hostedDone) return true;
  if (!hostedSetPins(H_CLK, H_CMD, H_D0, H_D1, H_D2, H_D3, H_RST)) return false;
  if (!hostedInitWiFi()) return false;
  hostedDone = true;
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wicfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_start();
  return true;
}

// ── Main setup wizard ──
void runWifiSetup() {
  int state = ST_INIT;
  bool redraw = true;
  g_alphaMode = true; g_shift = false;
  while (state != ST_DONE) {
    M5.update(); esp_task_wdt_reset();
    switch (state) {
      case ST_INIT:
        if (!initHostedWifi()) { state = ST_TZ_SELECT; g_tzDirty = true; break; }
        state = ST_WIFI_SCAN;
        break;
      case ST_WIFI_SCAN: {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        drawTitle("Scanning...");
        // Scanning animation — briefly show "Scanning..."
        d.fillScreen(TFT_BLACK);
        drawTitle("Scanning...");
        d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
        d.drawString("Scanning...", 540, 300);
        delay(400);
        g_scanCount = doScan();
        state = ST_WIFI_LIST; redraw = true;
        break;
      }
      case ST_WIFI_LIST:
        if (redraw) { drawListScreen(); redraw = false; }
        handleListScreen(state);
        if (state != ST_WIFI_LIST) { redraw = true; g_alphaMode = true; }
        break;
      case ST_KEYBOARD:
        if (g_kbFull) { drawKeyboardScreen(); g_kbFull = false; g_passDirty = false; }
        else if (g_passDirty) { drawPasswordOnly(); g_passDirty = false; }
        handleKeyboard(state);
        if (state != ST_KEYBOARD) { g_kbFull = true; }
        break;
      case ST_CONNECTING:
        if (redraw) { drawConnectingScreen(); redraw = false; doConnect(); }
        { uint32_t t = millis();
          while (millis() - t < 3000 && state == ST_CONNECTING) {
            M5.update(); esp_task_wdt_reset(); handleConnecting(state); delay(30);
          }
          if (state == ST_CONNECTING) {
            drawConnectingScreen();
            while (state == ST_CONNECTING) { M5.update(); esp_task_wdt_reset(); handleConnecting(state); delay(30); }
          }
        }
        redraw = true;
        break;
      case ST_TZ_SELECT:
        if (g_tzDirty) { drawTzScreen(); g_tzDirty = false; }
        handleTz(state);
        if (state != ST_TZ_SELECT) g_tzDirty = true;
        break;
      case ST_CONFIRM:
        drawConfirmScreen(g_tzOffset, state);
        break;
      default: break;
    }
    delay(10);
  }
  // Save
  { Preferences prefs; prefs.begin("cyberclock", false);
    prefs.putString("ssid", g_ssid); prefs.putString("pass", g_pass);
    prefs.putInt("tz", g_tzOffset); prefs.putBool("setup", true); prefs.end(); }
  delay(100); esp_task_wdt_reset();
}

// ── Re-run wizard (hosted stack already initialized) ──
void reconnectWifi() {
  int state = ST_WIFI_SCAN;
  bool redraw = true;
  g_alphaMode = true; g_shift = false; g_kbFull = true;
  esp_wifi_disconnect();
  Serial.println("[SETUP] Entered reconnectWifi");
  while (state != ST_DONE) {
    M5.update(); esp_task_wdt_reset();
    switch (state) {
      case ST_WIFI_SCAN: {
        auto& d = M5.Display;
        d.fillScreen(TFT_BLACK);
        drawTitle("Scanning...");
        // Scanning animation — briefly show "Scanning..."
        d.fillScreen(TFT_BLACK);
        drawTitle("Scanning...");
        d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
        d.drawString("Scanning...", 540, 300);
        delay(400);
        g_scanCount = doScan();
        state = ST_WIFI_LIST; redraw = true;
        break;
      }
      case ST_WIFI_LIST:
        if (redraw) { drawListScreen(); redraw = false; }
        handleListScreen(state);
        if (state != ST_WIFI_LIST) { redraw = true; g_alphaMode = true; }
        break;
      case ST_KEYBOARD:
        if (g_kbFull) { drawKeyboardScreen(); g_kbFull = false; g_passDirty = false; }
        else if (g_passDirty) { drawPasswordOnly(); g_passDirty = false; }
        handleKeyboard(state);
        if (state != ST_KEYBOARD) { g_kbFull = true; }
        break;
      case ST_CONNECTING:
        if (redraw) { drawConnectingScreen(); redraw = false; doConnect(); }
        { uint32_t t = millis();
          while (millis() - t < 3000 && state == ST_CONNECTING) {
            M5.update(); esp_task_wdt_reset(); handleConnecting(state); delay(30);
          }
          if (state == ST_CONNECTING) {
            drawConnectingScreen();
            while (state == ST_CONNECTING) { M5.update(); esp_task_wdt_reset(); handleConnecting(state); delay(30); }
          }
        }
        redraw = true;
        break;
      case ST_TZ_SELECT:
        if (g_tzDirty) { drawTzScreen(); g_tzDirty = false; }
        handleTz(state);
        if (state != ST_TZ_SELECT) g_tzDirty = true;
        break;
      case ST_CONFIRM:
        drawConfirmScreen(g_tzOffset, state);
        break;
      default: break;
    }
    delay(10);
  }
  // Save NVS + return
  { Preferences prefs; prefs.begin("cyberclock", false);
    prefs.putString("ssid", g_ssid); prefs.putString("pass", g_pass);
    prefs.putInt("tz", g_tzOffset); prefs.putBool("setup", true);
    prefs.end(); }
  g_tzCache = g_tzOffset;             // the clock reads RAM, not NVS
  delay(100); esp_task_wdt_reset();
}
bool wifiHasCreds() {
  Preferences prefs; prefs.begin("cyberclock", true);
  bool setup = prefs.getBool("setup", false);
  String ssid = prefs.getString("ssid", "");
  prefs.end();
  return setup && ssid.length() > 0;
}

// Can the RTC date the time at all right now? Used to decide whether a failed
// network sync should still boot into the clock.
bool timeRtcPlausible() {
  m5::rtc_datetime_t r;
  return rtcReadUTC(r);
}

bool autoConnectAndSync() {
  Preferences prefs; prefs.begin("cyberclock", true);
  String ssid = prefs.getString("ssid", ""); String pass = prefs.getString("pass", "");
  int32_t tz = prefs.getInt("tz", 28800); bool setup = prefs.getBool("setup", false);
  prefs.end();
  g_tzCache = tz;                     // keep the clock's RAM copy in sync
  if (!setup || ssid.length() == 0) return false;
  if (!initHostedWifi()) return false;

  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  UI::bar(d, "Matrix Rain");
  UI::centered(d, UI::T_BIG, "Connecting to WiFi ...", 300, MG::BODY);
  UI::hint(d, ssid.c_str(), 350);

  wifi_config_t cfg = {};
  strlcpy((char*)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid));
  strlcpy((char*)cfg.sta.password, pass.c_str(), sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN; cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  esp_wifi_disconnect(); delay(100);
  esp_wifi_set_config(WIFI_IF_STA, &cfg); esp_wifi_connect();

  bool connected = false;
  { uint32_t start = millis();
    while (millis() - start < 12000) {
      esp_task_wdt_reset();
      wifi_ap_record_t ap;
      if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) { connected = true; break; }
      delay(100);
    }
  }
  if (!connected) return false;

  d.fillScreen(TFT_BLACK);
  UI::bar(d, "Matrix Rain");
  UI::centered(d, UI::T_BIG, "Syncing time from the internet ...", 300, MG::BODY);

  struct tm t;
  if (!ntpSync((uint32_t)tz, 20000) || !getLocalTime(&t)) {
    Serial.printf("[TIME] NTP sync failed (status=%d)\n", (int)sntp_get_sync_status());
    return false;
  }
  g_tzOffset = tz;

  // ── Save UTC to hardware RTC (verified) ──
  time_t utcNow = time(nullptr);
  struct tm utcTm;
  gmtime_r(&utcNow, &utcTm);
  bool rtcOk = rtcWriteUTC(utcTm);
  Serial.printf("[TIME] ntp=%04d-%02d-%02d %02d:%02d:%02d  utc=%02d:%02d  rtc=%s write=%s\n",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
    utcTm.tm_hour, utcTm.tm_min,
    M5.Rtc.isEnabled() ? "on" : "off", rtcOk ? "ok" : "FAILED");

  // ── Show time confirmation (blocking) — the wizard's screen, verbatim ──
  return confirmTimeScreen(t, "Setup WiFi");
}

// ── Clock value for the display ──
// Derived from the system clock (a UTC epoch) plus the saved offset, so the
// display can only ever be as wrong as the source that set the system clock.
// The hardware RTC is NOT read here: it is only a seed, applied once when
// nothing authoritative exists — which keeps a stale RTC out of the display.
void updateClockFromNTP(int& h, int& m) {
  if (!g_timeTrusted && !g_rtcSeedTried) {
    g_rtcSeedTried = true;                 // one attempt per boot
    if (rtcSeedSystemTime()) {
      g_timeTrusted = true;
      Serial.println("[TIME] seeded from hardware RTC (no NTP)");
    } else {
      Serial.printf("[TIME] no valid time source (rtc=%s)\n",
                    M5.Rtc.isEnabled() ? "unusable" : "off");
    }
  }

  time_t now = time(nullptr);
  // g_timeTrusted gates this: M5.begin() seeds the system clock from the
  // hardware RTC, so a plausible-looking-but-garbage RTC value (e.g. 2084)
  // would otherwise sail through a year-range check alone.
  if (!g_timeTrusted || now < EPOCH_2025) { h = 0; m = 0; return; }

  int32_t tz = tzSeconds();

  // The epoch is UTC. Add the saved offset.
  int totalMin = (int)((now % 86400) / 60) + (int)(tz / 60);
  totalMin %= 1440;
  if (totalMin < 0) totalMin += 1440;

  h = totalMin / 60;
  m = totalMin % 60;
}
