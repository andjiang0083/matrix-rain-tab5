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
#include "matrix_gui.h"

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

// ── Helpers ──
static inline bool hitR(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

static void drawTitle(const char* s) {
  auto& d = M5.Display;
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextFont(2); d.setTextSize(2);
  d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString(s, 20, 10);
}

// Title with step indicator (e.g. "1/4  Select Network")
static void drawStepTitle(const char* step, const char* s) {
  auto& d = M5.Display;
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextFont(2); d.setTextSize(2);
  int16_t sw = d.textWidth(step);
  d.setTextColor(d.color565(0, MG::DIM, 0));
  d.drawString(step, 20, 10);
  d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString(s, 28 + sw, 10);
}

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

  // Scan button (secondary: subtle border)
  d.fillRoundRect(40, 680, 160, 36, 6, d.color565(0, MG::SEC, 0));
  d.drawRoundRect(40, 680, 160, 36, 6, d.color565(0, MG::SEC_BDR, 0));
  d.setTextFont(2); d.setTextSize(2); d.setTextColor(d.color565(0, MG::DIM, 0));
  d.drawString("[Scan]", 60, 688);

  // Back button (very subtle, right side)
  d.fillRoundRect(1080, 680, 160, 36, 6, d.color565(0, MG::CANCEL, 0));
  d.drawRoundRect(1080, 680, 160, 36, 6, d.color565(0, MG::CANCEL_BDR, 0));
  d.setTextColor(d.color565(0, MG::FAINT, 0));
  d.drawString("Back", 1120, 688);

  // Network list
  int yy = 56;
  for (int i = 0; i < g_scanCount && i < 12; i++) {
    uint16_t bg = (i == g_selNet) ? d.color565(0, MG::SEL, 0) : TFT_BLACK;
    d.fillRect(10, yy, 1260, 40, bg);

    // Cursor
    d.setTextFont(1); d.setTextSize(2); d.setTextColor(d.color565(0, MG::BRIGHT, 0));
    d.drawString((i == g_selNet) ? ">" : " ", 10, yy + 8);

    // SSID — sanitize non-ASCII → draw □ boxes
    char clean[65]; int replPos[16];
    const String& raw = g_scanSSIDs[i];
    int rc = sanitizeSSID(clean, 64, raw.c_str(), raw.length(), replPos, 16);
    d.setTextColor(d.color565(0, MG::BODY, 0));
    d.drawString(clean, 30, yy + 8);

    // Draw □ over non-ASCII replacement positions
    for (int r = 0; r < rc; r++) {
      char before[65]; strncpy(before, clean, replPos[r]); before[replPos[r]] = 0;
      int16_t bx = 30 + d.textWidth(before);
      d.drawRect(bx, yy + 10, 10, 14, d.color565(0, MG::REPLACE, 0));
    }

    // Signal bars
    int bars = constrain(map(g_scanRSSI[i], -90, -30, 1, 5), 1, 5);
    for (int b = 0; b < bars; b++)
      d.fillRect(1060 + b * 10, yy + 24 - b * 5, 8, b * 5 + 4, d.color565(0, map(b, 0, 4, MG::DIM, MG::BRIGHT), 0));
    // Unlit bars (dim)
    for (int b = bars; b < 5; b++)
      d.fillRect(1060 + b * 10, yy + 24 - b * 5, 8, b * 5 + 4, d.color565(0, MG::LINE, 0));

    // OPEN tag
    if (g_scanOpen[i]) {
      d.setTextColor(d.color565(0, MG::DIM, 0));
      d.drawString("OPEN", 1120, yy + 8);
    }
    yy += 44;
  }
}

static void handleListScreen(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (hitR(tx, ty, 40, 680, 160, 36)) { state = ST_WIFI_SCAN; waitRelease(); return; }
  if (hitR(tx, ty, 1080, 680, 160, 36)) { state = ST_DONE; waitRelease(); return; }
  int idx = (ty - 56) / 44;
  if (idx >= 0 && idx < g_scanCount && idx < 12) {
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
static const int K_BASE_Y = 152;
static const char* K_ALPHA[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const int K_ALPHA_LEN[] = { 10, 9, 7 };
static const char* K_NUM[] = { "1234567890", "-_@#.$%&+=", ",/:;\"'!?()" };
static const int K_NUM_LEN[] = { 10, 10, 10 };

static void drawKeyboardScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("2/4", "Enter Password");
  d.setTextFont(1); d.setTextSize(2); d.setTextColor(d.color565(0, MG::DIM, 0));
  d.drawString(g_ssid, 20, 56);
  // Password field
  d.fillRect(18, 82, 1244, 56, d.color565(0, MG::FIELD, 0));
  d.drawRect(18, 82, 1244, 56, d.color565(0, MG::LINE, 0));
  d.setTextFont(1); d.setTextSize(2);
  if (g_passLen == 0) {
    d.setTextColor(d.color565(0, MG::DIM, 0)); d.drawString("tap keys above", 30, 98);
  } else {
    d.setTextColor(d.color565(0, MG::BODY, 0));
    char buf[65]; int len = g_passLen < 64 ? g_passLen : 64;
    memcpy(buf, g_pass, len); buf[len] = 0;
    if (d.textWidth(buf) > 1220) {
      int show = 50; if (show > len) show = len;
      memcpy(buf, g_pass + len - show, show);
      memmove(buf + 3, buf, show);
      buf[0]='.'; buf[1]='.'; buf[2]='.'; buf[show+3]=0;
    }
    d.drawString(buf, 24, 98);
  }
  // Letter / number keys
  const char** rows; const int* rowLens; int numRows = 3;
  if (g_alphaMode) { rows = K_ALPHA; rowLens = K_ALPHA_LEN; }
  else             { rows = K_NUM;   rowLens = K_NUM_LEN; }
  for (int r = 0; r < numRows; r++) {
    int yy = K_BASE_Y + r * (K_SZ + K_GAP);
    int totalW = rowLens[r] * K_SZ + (rowLens[r] - 1) * K_GAP;
    int xx = (1280 - totalW) / 2;
    for (int k = 0; k < rowLens[r]; k++) {
      char ch = rows[r][k];
      if (g_alphaMode && g_shift) ch = toupper(ch);
      d.fillRoundRect(xx, yy, K_SZ, K_SZ, 6, d.color565(0, MG::HOVER, 0));
      d.drawRoundRect(xx, yy, K_SZ, K_SZ, 6, d.color565(0, MG::LINE, 0));
      d.setTextColor(d.color565(0, MG::BODY, 0)); d.setTextSize(2); d.setTextFont(1);
      char lbl[2] = {ch, 0};
      int16_t tw = d.textWidth(lbl);
      d.drawString(lbl, xx + (K_SZ - tw) / 2, yy + (K_SZ - 20) / 2);
      xx += K_SZ + K_GAP;
    }
  }
  // Bottom row
  int by = K_BASE_Y + numRows * (K_SZ + K_GAP);
  int lx = 20;
  // Shift
  uint16_t shiftBg = g_shift ? d.color565(0, MG::KB_SHIFT, 0) : d.color565(0, MG::SEC, 0);
  d.fillRoundRect(lx, by, 80, K_SZ, 6, shiftBg);
  d.drawRoundRect(lx, by, 80, K_SZ, 6, d.color565(0, MG::SEC_BDR, 0));
  d.setTextColor(d.color565(0, MG::BRIGHT, 0)); d.drawString("^", lx + 28, by + (K_SZ-20)/2);
  lx += 86;
  // 123 toggle
  d.fillRoundRect(lx, by, 100, K_SZ, 6, d.color565(MG::KB_MODE_R, MG::KB_MODE_G, MG::KB_MODE_B));
  d.drawRoundRect(lx, by, 100, K_SZ, 6, d.color565(0, MG::SEC_BDR, 0));
  d.setTextColor(d.color565(0, MG::BODY, 0)); d.drawString(g_alphaMode?"123#":"ABC", lx+12, by+(K_SZ-20)/2);
  lx += 106;
  // Space
  int remain = 1280 - lx - 20 - 106 - 180;
  d.fillRoundRect(lx, by, remain, K_SZ, 6, d.color565(0, MG::SEC, 0));
  d.drawRoundRect(lx, by, remain, K_SZ, 6, d.color565(0, MG::LINE, 0));
  d.setTextColor(d.color565(0, MG::BODY, 0)); d.drawString("Space", lx + remain/2 - 40, by+(K_SZ-20)/2);
  lx += remain + 6;
  // Backspace
  d.fillRoundRect(lx, by, 100, K_SZ, 6, d.color565(MG::KB_DEL_R, MG::SEC, 0));
  d.drawRoundRect(lx, by, 100, K_SZ, 6, d.color565(0, MG::SEC_BDR, 0));
  d.setTextColor(d.color565(0, MG::DIM, 0)); d.drawString("DEL", lx + 16, by+(K_SZ-20)/2);
  lx += 106;
  // Connect (primary button)
  d.fillRoundRect(lx, by, 180, K_SZ, 6, d.color565(0, MG::BTN, 0));
  d.drawRoundRect(lx, by, 180, K_SZ, 6, d.color565(0, MG::BTN_BDR, 0));
  d.setTextColor(d.color565(0, MG::BRIGHT, 0)); d.drawString("Connect", lx+14, by+(K_SZ-20)/2);
  // Cancel (low-contrast)
  d.fillRoundRect(20, 660, 120, 44, 6, d.color565(0, MG::CANCEL, 0));
  d.drawRoundRect(20, 660, 120, 44, 6, d.color565(0, MG::CANCEL_BDR, 0));
  d.setTextColor(d.color565(0, MG::DIM, 0)); d.setTextSize(2); d.drawString("Cancel", 36, 672);
}

// ── Draw only the password field (fast, no flicker) ──
static void drawPasswordOnly() {
  auto& d = M5.Display;
  d.fillRect(18, 82, 1244, 56, d.color565(0, MG::FIELD, 0));
  d.drawRect(18, 82, 1244, 56, d.color565(0, MG::LINE, 0));
  d.setTextFont(1); d.setTextSize(2);
  if (g_passLen == 0) {
    d.setTextColor(d.color565(0, MG::DIM, 0)); d.drawString("tap keys above", 30, 98);
  } else {
    d.setTextColor(d.color565(0, MG::BODY, 0));
    char buf[65]; int len = g_passLen < 64 ? g_passLen : 64;
    memcpy(buf, g_pass, len); buf[len] = 0;
    if (d.textWidth(buf) > 1220) {
      int show = 50; if (show > len) show = len;
      memcpy(buf, g_pass + len - show, show);
      memmove(buf + 3, buf, show);
      buf[0]='.'; buf[1]='.'; buf[2]='.'; buf[show+3]=0;
    }
    d.drawString(buf, 24, 98);
  }
}

static void handleKeyboard(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (hitR(tx, ty, 20, 660, 120, 44)) { state = ST_WIFI_LIST; waitRelease(); return; }
  int by = K_BASE_Y + 3 * (K_SZ + K_GAP);
  int lx = 20;
  // Shift  → full redraw
  if (hitR(tx, ty, lx, by, 80, K_SZ)) { g_shift = !g_shift; g_kbFull = true; waitRelease(); return; }
  lx += 86;
  // 123 toggle → full redraw
  if (hitR(tx, ty, lx, by, 100, K_SZ)) { g_alphaMode = !g_alphaMode; g_shift = false; g_kbFull = true; waitRelease(); return; }
  lx += 106;
  // Space → password only
  int remain = 1280 - lx - 20 - 106 - 180;
  if (hitR(tx, ty, lx, by, remain, K_SZ)) { if (g_passLen < 120) { g_pass[g_passLen++] = ' '; g_passDirty = true; } waitRelease(); return; }
  lx += remain + 6;
  // Backspace → password only
  if (hitR(tx, ty, lx, by, 100, K_SZ)) { if (g_passLen > 0) { g_pass[--g_passLen] = 0; g_passDirty = true; } waitRelease(); return; }
  lx += 106;
  // Connect → transition
  if (hitR(tx, ty, lx, by, 180, K_SZ)) { if (g_passLen > 0) { g_pass[g_passLen] = 0; state = ST_CONNECTING; } waitRelease(); return; }
  // Letter/number keys → password only
  const char** rows; const int* rowLens;
  if (g_alphaMode) { rows = K_ALPHA; rowLens = K_ALPHA_LEN; }
  else             { rows = K_NUM;   rowLens = K_NUM_LEN; }
  for (int r = 0; r < 3; r++) {
    int yy = K_BASE_Y + r * (K_SZ + K_GAP);
    int totalW = rowLens[r] * K_SZ + (rowLens[r] - 1) * K_GAP;
    int xx = (1280 - totalW) / 2;
    for (int k = 0; k < rowLens[r]; k++) {
      char ch = rows[r][k];
      if (g_alphaMode && g_shift) ch = toupper(ch);
      if (hitR(tx, ty, xx, yy, K_SZ, K_SZ)) {
        if (g_passLen < 120) { g_pass[g_passLen++] = ch; g_passDirty = true; }
        if (g_alphaMode && g_shift) g_shift = false;
        waitRelease(); return;
      }
      xx += K_SZ + K_GAP;
    }
  }
}

// ── Screen: Connecting ──
static void drawConnectingScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("3/4", "Connecting...");
  d.setTextFont(1); d.setTextSize(2);
  d.setTextColor(d.color565(0, MG::DIM, 0));
  char buf[128]; snprintf(buf, sizeof(buf), "\"%s\"", g_ssid);
  d.drawString(buf, 40, 100);
  d.setTextColor(d.color565(0, MG::BODY, 0));
  d.drawString("Connecting...", 40, 160);
  wifi_ap_record_t ap; bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
  if (connected) {
    d.setTextColor(d.color565(0, MG::BRIGHT, 0));
    d.drawString("Connected!", 40, 210);
    d.setTextColor(d.color565(0, MG::DIM, 0));
    d.drawString("touch to continue", 40, 280);
  }
  // Cancel (low-contrast)
  d.fillRoundRect(40, 590, 160, 40, 6, d.color565(0, MG::CANCEL, 0));
  d.drawRoundRect(40, 590, 160, 40, 6, d.color565(0, MG::CANCEL_BDR, 0));
  d.setTextColor(d.color565(0, MG::DIM, 0)); d.drawString("Cancel", 60, 598);
}

static void handleConnecting(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  if (hitR(tx, ty, 40, 590, 160, 40)) { esp_wifi_disconnect(); state = ST_WIFI_LIST; waitRelease(); return; }
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) { state = ST_TZ_SELECT; g_tzDirty = true; waitRelease(); }
}

// ── Screen: Timezone ──
static void drawTzScreen() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawStepTitle("4/4", "Select Timezone");
  d.setTextSize(2);
  int cols = 6, bw = 160, bh = 40, gap = 10;
  int startX = (1280 - (cols * bw + (cols - 1) * gap)) / 2;
  int startY = 120;
  for (int i = 0; i < TZ_COUNT; i++) {
    int bx = startX + (i % cols) * (bw + gap);
    int by = startY + (i / cols) * (bh + gap);
    bool sel = (i == g_tzIdx);
    d.fillRoundRect(bx, by, bw, bh, 4, sel ? d.color565(0, MG::SEL, 0) : d.color565(0, MG::HOVER, 0));
    d.drawRoundRect(bx, by, bw, bh, 4, sel ? d.color565(0, MG::BTN_BDR, 0) : d.color565(0, MG::LINE, 0));
    d.setTextColor(sel ? d.color565(0, MG::BRIGHT, 0) : d.color565(0, MG::BODY, 0));
    char lbl[16]; snprintf(lbl, sizeof(lbl), "%s%s", sel ? ">" : "", TZ_LIST[i].label);
    d.drawString(lbl, bx + 4, by + 12);
  }
  // Done button (primary)
  d.fillRoundRect(540, 560, 200, 48, 6, d.color565(0, MG::BTN, 0));
  d.drawRoundRect(540, 560, 200, 48, 6, d.color565(0, MG::BTN_BDR, 0));
  d.setTextFont(2); d.setTextSize(2); d.setTextColor(d.color565(0, MG::BRIGHT, 0));
  { int tw = d.textWidth("Done");
    d.drawString("Done", 540 + (200 - tw) / 2, 560 + (48 - 16) / 2); }
}

static void handleTz(int& state) {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  // Done button → confirm screen (not ST_DONE)
  if (hitR(tx, ty, 540, 560, 200, 48)) { g_tzOffset = TZ_LIST[g_tzIdx].offset; state = ST_CONFIRM; waitRelease(); return; }
  int cols = 6, bw = 160, bh = 40, gap = 10;
  int startX = (1280 - (cols * bw + (cols - 1) * gap)) / 2;
  int startY = 120;
  for (int i = 0; i < TZ_COUNT; i++) {
    int bx = startX + (i % cols) * (bw + gap);
    int by = startY + (i / cols) * (bh + gap);
    if (hitR(tx, ty, bx, by, bw, bh)) { g_tzIdx = i; g_tzOffset = TZ_LIST[i].offset; g_tzDirty = true; waitRelease(); return; }
  }
}

// ── Screen: Time confirmation (NTP sync + display) ──
static void drawConfirmScreen(int32_t tz, int& state) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  drawTitle("Syncing time from internet...");
  d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
  d.drawString("Contacting time servers...", 400, 260);
  
  // NTP sync
  configTime(tz, 0, "pool.ntp.org", "time.google.com");
  struct tm t; int n = 0;
  while (!getLocalTime(&t) && n < 20) { delay(500); n++; esp_task_wdt_reset(); }
  
  // Show result
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextFont(2); d.setTextSize(2); d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString("Confirm Current Time", 20, 10);
  
  if (getLocalTime(&t)) {
    // ── Save UTC to hardware RTC ──
    { time_t utcNow = time(nullptr); struct tm utcTm; gmtime_r(&utcNow, &utcTm);
      m5::rtc_datetime_t rdt;
      rdt.date.year = utcTm.tm_year+1900; rdt.date.month = utcTm.tm_mon+1;
      rdt.date.date = utcTm.tm_mday; rdt.time.hours = utcTm.tm_hour;
      rdt.time.minutes = utcTm.tm_min; rdt.time.seconds = utcTm.tm_sec;
      rdt.date.weekDay = utcTm.tm_wday; M5.Rtc.setDateTime(rdt); }
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
      t.tm_year+1900, t.tm_mon+1, t.tm_mday,
      t.tm_hour, t.tm_min, t.tm_sec);
    d.setTextSize(5); d.setTextColor(d.color565(MG::ACCENT_R, MG::ACCENT_G, MG::ACCENT_B));
    const int timeW = d.textWidth("0000-00-00  00:00:00");
    const int timeY = 180, timeH = 80;
    const int timeX = (1280 - timeW) / 2;
    d.drawString(buf, timeX, timeY);
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
    d.drawString("Time synced successfully", 440, 320);

    // Confirm (primary) + Reselect (secondary) buttons
    d.setTextFont(2);
    const int btnW = 240, btnH = 52, btnGap = 60;
    int cx = 1280 / 2;
    int bx1 = cx - btnW - btnGap / 2;
    int bx2 = cx + btnGap / 2;
    int by = 440;

    d.fillRoundRect(bx1, by, btnW, btnH, 6, d.color565(0, MG::BTN, 0));
    d.drawRoundRect(bx1, by, btnW, btnH, 6, d.color565(0, MG::BTN_BDR, 0));
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::BRIGHT, 0));
    { int tw = d.textWidth("Confirm");
      d.drawString("Confirm", bx1 + (btnW - tw) / 2, by + (btnH - 16) / 2); }

    d.fillRoundRect(bx2, by, btnW, btnH, 6, d.color565(0, MG::SEC, 0));
    d.drawRoundRect(bx2, by, btnW, btnH, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextColor(d.color565(0, MG::DIM, 0));
    { int tw = d.textWidth("Reselect");
      d.drawString("Reselect", bx2 + (btnW - tw) / 2, by + (btnH - 16) / 2); }

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
        d.setTextFont(2); d.setTextSize(5); d.setTextColor(d.color565(MG::ACCENT_R, MG::ACCENT_G, MG::ACCENT_B));
        d.drawString(nb, timeX, timeY);
      }

      int tx, ty;
      if (getTouch(tx, ty)) {
        if (hitR(tx, ty, bx1, by, btnW, btnH)) { waitRelease(); state = ST_DONE; return; }
        if (hitR(tx, ty, bx2, by, btnW, btnH)) { waitRelease(); state = ST_TZ_SELECT; g_tzDirty = true; return; }
      }
      delay(20);
    }
  } else {
    // NTP failed → Retry (warning tone)
    d.setTextSize(2); d.setTextColor(d.color565(MG::WARN_R, MG::WARN_G, 0));
    d.drawString("NTP time sync failed!", (1280 - d.textWidth("NTP time sync failed!")) / 2, 260);
    d.setTextSize(1); d.setTextColor(d.color565(0, MG::DIM, 0));
    d.drawString("Touch below to retry", (1280 - d.textWidth("Touch below to retry")) / 2, 320);

    d.setTextFont(2);
    const int btnW = 300, btnH = 52;
    int bx = (1280 - btnW) / 2, by = 440;
    d.fillRoundRect(bx, by, btnW, btnH, 6, d.color565(0, MG::WARN_BG, 0));
    d.drawRoundRect(bx, by, btnW, btnH, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::WARN_G, 0));
    { int tw = d.textWidth("Retry");
      d.drawString("Retry", bx + (btnW - tw) / 2, by + (btnH - 16) / 2); }

    while (true) {
      M5.update(); esp_task_wdt_reset();
      int tx, ty;
      if (getTouch(tx, ty)) {
        if (hitR(tx, ty, bx, by, btnW, btnH)) { waitRelease();
          state = ST_TZ_SELECT; g_tzDirty = true; return; }
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
  delay(100); esp_task_wdt_reset();
}
bool autoConnectAndSync() {
  Preferences prefs; prefs.begin("cyberclock", true);
  String ssid = prefs.getString("ssid", ""); String pass = prefs.getString("pass", "");
  int32_t tz = prefs.getInt("tz", 28800); bool setup = prefs.getBool("setup", false);
  prefs.end();
  if (!setup || ssid.length() == 0) return false;
  if (!initHostedWifi()) return false;

  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextFont(2); d.setTextSize(2); d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString("Matrix Rain", 20, 10);
  d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
  d.drawString("Connecting to WiFi...", 440, 300);
  d.setTextSize(1); d.setTextColor(d.color565(0, MG::DIM, 0));
  d.drawString(ssid.c_str(), 480, 340);

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
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextSize(2); d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString("Matrix Rain", 20, 10);
  d.drawString("Syncing time from internet...", 380, 300);

  configTime(tz, 0, "pool.ntp.org", "time.google.com");
  struct tm t; int ntpTries = 0;
  while (!getLocalTime(&t) && ntpTries < 30) { delay(500); ntpTries++; esp_task_wdt_reset(); }

  if (!getLocalTime(&t)) { return false; }

  // ── Save UTC to hardware RTC ──
  time_t utcNow = time(nullptr);
  struct tm utcTm;
  gmtime_r(&utcNow, &utcTm);
  {
    m5::rtc_datetime_t rdt;
    rdt.date.year = utcTm.tm_year + 1900;
    rdt.date.month = utcTm.tm_mon + 1;
    rdt.date.date = utcTm.tm_mday;
    rdt.time.hours = utcTm.tm_hour;
    rdt.time.minutes = utcTm.tm_min;
    rdt.time.seconds = utcTm.tm_sec;
    rdt.date.weekDay = utcTm.tm_wday;
    M5.Rtc.setDateTime(rdt);
    g_tzOffset = tz;
  }

  // ── Show time confirmation (blocking) ──
  d.fillScreen(TFT_BLACK);
  drawTitle("Matrix Rain");

  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
    utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
    t.tm_hour, t.tm_min, t.tm_sec);
  d.setTextFont(2); d.setTextSize(5); d.setTextColor(d.color565(MG::ACCENT_R, MG::ACCENT_G, MG::ACCENT_B));
  const int timeY = 180, timeH = 80;
  const int timeW = d.textWidth("0000-00-00  00:00:00");  // fixed max width
  const int timeX = (1280 - timeW) / 2;
  d.drawString(buf, timeX, timeY);
  d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
  d.drawString("Time synced successfully", (1280 - d.textWidth("Time synced successfully")) / 2, 320);
  d.setTextFont(2);

  // Confirm / Reselect buttons — auto-centered
  const int btnW = 240, btnH = 52, btnGap = 60;
  int cx = 1280 / 2;
  int bx1 = cx - btnW - btnGap / 2;
  int bx2 = cx + btnGap / 2;
  int by = 440;

  d.fillRoundRect(bx1, by, btnW, btnH, 6, d.color565(0, MG::BTN, 0));
  d.drawRoundRect(bx1, by, btnW, btnH, 6, d.color565(0, MG::BTN_BDR, 0));
  d.setTextSize(2); d.setTextColor(d.color565(0, MG::BRIGHT, 0));
  { int tw = d.textWidth("Confirm");
    d.drawString("Confirm", bx1 + (btnW - tw) / 2, by + (btnH - 16) / 2); }

  d.fillRoundRect(bx2, by, btnW, btnH, 6, d.color565(0, MG::SEC, 0));
  d.drawRoundRect(bx2, by, btnW, btnH, 6, d.color565(0, MG::SEC_BDR, 0));
  d.setTextColor(d.color565(0, MG::DIM, 0));
  { int tw = d.textWidth("Setup WiFi");
    d.drawString("Setup WiFi", bx2 + (btnW - tw) / 2, by + (btnH - 16) / 2); }

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
      d.setTextFont(2); d.setTextSize(5); d.setTextColor(d.color565(MG::ACCENT_R, MG::ACCENT_G, MG::ACCENT_B));
      d.drawString(nb, timeX, timeY);
    }

    int tx, ty;
    if (getTouch(tx, ty)) {
      if (hitR(tx, ty, bx1, by, btnW, btnH)) { waitRelease(); return true; }
      if (hitR(tx, ty, bx2, by, btnW, btnH)) { waitRelease(); return false; }
    }
    delay(20);
  }
}

// ── Update clock from NVS timezone offset ──
// Reads UTC system time (set by boot NTP) and applies saved offset.
// No configTime/setenv/SNTP calls — completely safe for repeated use.
void updateClockFromNTP(int& h, int& m) {
  auto dt = M5.Rtc.getDateTime();
  // Sanity check
  if (dt.date.year < 2025 || dt.date.year > 2099) { h = 0; m = 0; return; }

  // Read timezone offset from NVS
  Preferences prefs;
  prefs.begin("cyberclock", true);
  int32_t tz = prefs.getInt("tz", 28800);
  prefs.end();

  // RTC stores UTC. Add timezone offset.
  int totalMin = dt.time.hours * 60 + dt.time.minutes + tz / 60;
  totalMin %= (24 * 60);
  if (totalMin < 0) totalMin += 24 * 60;

  h = totalMin / 60;
  m = totalMin % 60;
}
