// ────────────────────────────────────────────────────────────
// CYBER CLOCK — M5Stack Tab5 — Matrix Rain v1.3.1
// 80 cols × 6 char sets, 30fps, NTP time sync, ghost-character glow clock, WiFi setup
// ────────────────────────────────────────────────────────────

#include <M5Unified.h>
#include "config.h"
#include "katakana_font.h"
#include "wifi_setup.h"
#include "setup_menu.h"
#include "matrix_gui.h"
#include <Preferences.h>
#include <driver/ledc.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>

// ── Sprite (only used for screenshots) ──
static LGFX_Sprite canvas(&M5.Display);
static bool g_doSnap = false;

// ── Character Sets ──
struct CharSet {
  const char* name;
  const uint16_t* chars;
  int numChars;
  bool isKatakana;  // true = use bitmap font, false = use GLCD
};

// GLCD sets (ASCII)
static const uint16_t CS_FULL[] = {
  '0','1','#','*','+',':','.','A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f','g',
  'h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
  '<','>','/','|','~','@','%','&','$'
};
static const int CS_FULL_N = sizeof(CS_FULL)/sizeof(CS_FULL[0]);

static const uint16_t CS_NUM[] = { '0','1','2','3','4','5','6','7','8','9' };
static const int CS_NUM_N = 10;

static const uint16_t CS_HEX[] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
static const int CS_HEX_N = 16;

static const uint16_t CS_BIN[] = { '0','1' };
static const int CS_BIN_N = 2;

static const uint16_t CS_ALPHA[] = {
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
};
static const int CS_ALPHA_N = 26;

// Katakana + kanji bitmap set (uses kata_codes[] from katakana_font.h)
// We reference kata_codes directly as the character pool
static const int CS_KATA_N = KATA_N;

static const CharSet g_charSets[] = {
  { "FULL",     CS_FULL,  CS_FULL_N,  false },
  { "NUM",      CS_NUM,   CS_NUM_N,   false },
  { "HEX",      CS_HEX,   CS_HEX_N,   false },
  { "BIN",      CS_BIN,   CS_BIN_N,   false },
  { "ALPHA",    CS_ALPHA, CS_ALPHA_N, false },
  { "KATAKANA", kata_codes, CS_KATA_N, true },
};
static const int CS_COUNT = sizeof(g_charSets)/sizeof(g_charSets[0]);
static int g_curSet = 0;

// ── Clock (refraction clock, hardcoded time for now) ──
static int g_h = 12, g_m = 34;

// 7-segment active mask [digit][segment: a,b,c,d,e,f,g]
// a=top, b=top-right, c=bot-right, d=bottom, e=bot-left, f=top-left, g=middle
static const bool SEG_ON[10][7] = {
  {1,1,1,1,1,1,0},  // 0
  {0,1,1,0,0,0,0},  // 1
  {1,1,0,1,1,0,1},  // 2
  {1,1,1,1,0,0,1},  // 3
  {0,1,1,0,0,1,1},  // 4
  {1,0,1,1,0,1,1},  // 5
  {1,0,1,1,1,1,1},  // 6
  {1,1,1,0,0,0,0},  // 7
  {1,1,1,1,1,1,1},  // 8
  {1,1,1,1,0,1,1},  // 9
};

// 7 segment rects relative to digit top-left (digit cell: 230w × 272h)
struct SegRect { int16_t x, y, w, h; };
static const SegRect SEG_RECTS[7] = {
  { 29,  0,   172, 23 },   // a: top
  { 197, 20,  33,  116 },  // b: top-right
  { 197, 136, 33,  116 },  // c: bottom-right
  { 29,  249, 172, 23 },   // d: bottom
  {  0,  136, 33,  116 },  // e: bottom-left
  {  0,  20,  33,  116 },  // f: top-left
  { 29,  124, 172, 23 },   // g: middle
};

// Colon rects (always active, between D1 and D2)
// D1 ends at x=594, colon centered in 92px gap at x=628
static const SegRect COLON_RECTS[2] = {
  { 628, 328, 16, 16 },   // top dot
  { 628, 368, 16, 16 },   // bottom dot
};

// Digit cell positions (top-left)
// Digit cell: 230w×272h (another 20% wider), gaps: 34px, colon: 24px
// Total width: 4×230 + 4×34 + 24 = 1080, start: (1280-1080)/2 = 100
static const int DIGIT_X[4] = { 100, 364, 686, 950 };
static const int DIGIT_Y   = 224;

// Absolute segment rects for each digit, computed in setup()
static SegRect g_absSegs[4][7];

// Trail glow buffer (persistence effect inside clock segments)
// [column][row16] = brightness 0-255, decayed each frame
static uint8_t g_clockGlow[80][45];  // 80 cols × 45 rows (720/16)
static uint16_t g_glowChar[80][45];  // character code displayed at each glow cell

// ── Matrix Rain ──
static const int   COLS       = 80;
static const int   MAX_TRAIL  = 18;
static const int   FPS_LIMIT  = 30;
static const uint32_t FRAME_MS = 1000 / FPS_LIMIT;
static const float SPEED_BASE = 140.0f;
static const int   ROW_H      = 16;  // both GLCD@2× and 16×16 bitmaps
static const float MAX_Y      = (float)(SCREEN_H + MAX_TRAIL * ROW_H);
static const float IDLE_TOP   = (float)(MAX_TRAIL * ROW_H);

// ── Green trail color computation ──
// Trail[t] = fade(t) * head + (1-fade(t)) * dark_green
// fade(t) = 0.15 + 0.85 * (1 - t/(MAX_TRAIL-1))^2
static uint16_t g_trailCol[18];  // MAX_TRAIL
static void rebuildTrailColors() {
  const uint8_t gr = 2, gg = 8, gb = 1;
  for (int t = 0; t < MAX_TRAIL; t++) {
    float fade = 1.0f - (float)t / (float)(MAX_TRAIL - 1);
    float headW = 0.15f + 0.85f * fade * fade;
    float darkW = 1.0f - headW;
    int r = (int)(19 * headW + gr * darkW);
    int g = (int)(63 * headW + gg * darkW);
    int b = (int)(8 * headW + gb * darkW);
    g_trailCol[t] = ((uint16_t)(r & 0x3F) << 11) | ((uint16_t)(g & 0x3F) << 5) | (b & 0x1F);
  }
}

static float randF(float a, float b) { return a + (b-a) * ((float)rand()/(float)RAND_MAX); }

static uint16_t rchar() {
  const auto& cs = g_charSets[g_curSet];
  return cs.chars[rand() % cs.numChars];
}

struct RainCol {
  float y;
  float prevY;
  uint16_t trail[MAX_TRAIL];
  float speed;
  int trailLen;
  float depth;
};
static RainCol g_col[COLS];

static void initRain() {
  for (int c = 0; c < COLS; c++) {
    g_col[c].depth = randF(0.6f, 1.5f);
    g_col[c].trailLen = 2 + (int)(g_col[c].depth * 11);
    if (g_col[c].trailLen > MAX_TRAIL) g_col[c].trailLen = MAX_TRAIL;
    g_col[c].y = -randF(0, IDLE_TOP);
    g_col[c].prevY = g_col[c].y;
    g_col[c].speed = SPEED_BASE * g_col[c].depth * randF(0.7f, 1.3f);
    for (int t = 0; t < MAX_TRAIL; t++) g_col[c].trail[t] = rchar();
  }
}

static void reinitRain() {
  for (int c = 0; c < COLS; c++) {
    for (int t = 0; t < MAX_TRAIL; t++) g_col[c].trail[t] = rchar();
  }
}

static uint32_t g_lastT = 0;
static void updateRain() {
  uint32_t now = millis();
  float dt = (now - g_lastT) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;
  g_lastT = now;
  for (int c = 0; c < COLS; c++) {
    g_col[c].y += g_col[c].speed * dt;
    if (g_col[c].y > MAX_Y) { g_col[c].y = -randF(0, IDLE_TOP); }
    for (int t = 0; t < g_col[c].trailLen; t++) {
      int chance = t * t + 2;
      if (chance > 120) chance = 120;
      if (rand() % chance == 0) g_col[c].trail[t] = rchar();
    }
  }
}

// ── Katakana bitmap font renderer ──
// Draws a 16×16 1bpp glyph using horizontal run-length fillRect calls.
template <typename T>
static void drawKatakana(T& out, uint16_t code, int x, int y, uint16_t color) {
  int idx = kata_index(code);
  if (idx < 0) return;
  const uint16_t* glyph = kata_bitmaps[idx];
  for (int py = 0; py < 16; py++) {
    uint16_t row = glyph[py];
    int runStart = -1;
    for (int px = 0; px < 16; px++) {
      bool set = (row >> (15 - px)) & 1;
      if (set) {
        if (runStart < 0) runStart = px;
      } else {
        if (runStart >= 0) {
          out.fillRect(x + runStart, y + py, px - runStart, 1, color);
          runStart = -1;
        }
      }
    }
    if (runStart >= 0) {
      out.fillRect(x + runStart, y + py, 16 - runStart, 1, color);
    }
  }
}

// ── Clock segment check ──
static void initClockSegments() {
  for (int d = 0; d < 4; d++) {
    for (int s = 0; s < 7; s++) {
      g_absSegs[d][s].x = DIGIT_X[d] + SEG_RECTS[s].x;
      g_absSegs[d][s].y = DIGIT_Y    + SEG_RECTS[s].y;
      g_absSegs[d][s].w = SEG_RECTS[s].w;
      g_absSegs[d][s].h = SEG_RECTS[s].h;
    }
  }
}

// Check if a 16×16 character at (cx, cy) overlaps any active clock segment.
// Sets ox, oy = refraction offset and adjusts color for perception.
// Segments push inward: top↓ bottom↑ left→ right←, creating a "lens" effect.
static bool getRefraction(int cx, int cy, int& ox, int& oy, uint16_t& color) {
  int v[] = { g_h/10, g_h%10, g_m/10, g_m%10 };
  for (int d = 0; d < 4; d++) {
    for (int s = 0; s < 7; s++) {
      if (!SEG_ON[v[d]][s]) continue;
      auto& r = g_absSegs[d][s];
      if (cx < r.x + r.w && cx + 16 > r.x &&
          cy < r.y + r.h && cy + 16 > r.y) {
        // ── Inward-pushing offset per segment ──
        switch (s) {
          case 0:  ox = 0;  oy = 4;  break;  // a: top      ↓
          case 1:  ox = -4; oy = 0;  break;  // b: top-R    ←
          case 2:  ox = -4; oy = 0;  break;  // c: bot-R    ←
          case 3:  ox = 0;  oy = -4; break;  // d: bottom   ↑
          case 4:  ox = 4;  oy = 0;  break;  // e: bot-L    →
          case 5:  ox = 4;  oy = 0;  break;  // f: top-L    →
          case 6:  ox = 0;  oy = 4;  break;  // g: middle   ↓
        }
        // ── Brightness (+25%) + hue shift ──
        int r = ((color >> 11) & 0x1F);
        int g = ((color >>  5) & 0x3F);
        int b = ( color        & 0x1F);
        // Top segments (a, f, g): warmer (more R)
        if (s == 0 || s == 5 || s == 6) { r = r * 7 / 5; g = g * 6 / 5; }
        // Bottom segments (d, e, c): cooler (more B)
        else if (s == 3 || s == 4 || s == 2) { b = b * 7 / 5; g = g * 6 / 5; }
        // Right-side verticals (b): slightly brighter green
        else { g = g * 7 / 5; }
        if (r > 31) r = 31;
        if (g > 63) g = 63;
        if (b > 31) b = 31;
        color = (r << 11) | (g << 5) | b;
        return true;
      }
    }
  }
  // ── Check colon (always active) ──
  for (int s = 0; s < 2; s++) {
    auto& r = COLON_RECTS[s];
    if (cx < r.x + r.w && cx + 16 > r.x &&
        cy < r.y + r.h && cy + 16 > r.y) {
      ox = 4; oy = 0;  // colon pushes rain rightward
      int rr = ((color >> 11) & 0x1F) * 7 / 5;
      int gg = ((color >>  5) & 0x3F) * 7 / 5;
      int bb = ( color        & 0x1F) * 7 / 5;
      if (rr > 31) rr = 31; if (gg > 63) gg = 63; if (bb > 31) bb = 31;
      color = (rr << 11) | (gg << 5) | bb;
      return true;
    }
  }
  return false;
}

// ── Draw ──
template <typename T>
static void drawRainOn(T& out) {
  const bool isKata = g_charSets[g_curSet].isKatakana;

  // Reset font for non-katakana (WiFi UI may have changed font state)
  if (!isKata) {
    out.setTextFont(1);
    out.setTextSize(2);
  }

  // Decay glow: moderate rate retains FIFO gradient + persistence
  for (int gc = 0; gc < COLS; gc++) {
    for (int gr = 0; gr < 45; gr++) {
      uint8_t v = g_clockGlow[gc][gr];
      if (!v) continue;
      v = (v > 1) ? v - 1 : 0;  // 50% longer (127→255 frames = ~8.5s)
      g_clockGlow[gc][gr] = v;
    }
  }

  out.startWrite();
  for (int c = 0; c < COLS; c++) {
    int headPx = (int)g_col[c].y;
    int tl = g_col[c].trailLen;
    int trailTop = headPx - (tl - 1) * ROW_H;
    if (headPx < 0 || trailTop >= SCREEN_H) continue;

    int x = c * 16 + 2;
    int oldTop  = (int)g_col[c].prevY - (tl - 1) * ROW_H;
    int newTop  = trailTop;
    int newBot  = headPx + ROW_H + 8;
    int oldBot  = (int)g_col[c].prevY + ROW_H + 8;
    int clearTop = (newTop < oldTop ? newTop : oldTop) - ROW_H;
    int clearBot = (newBot > oldBot ? newBot : oldBot);
    int cy = clearTop, ch = clearBot - clearTop;
    if (cy < 0) { ch += cy; cy = 0; }
    if (cy + ch > SCREEN_H) ch = SCREEN_H - cy;
    if (ch > 0) out.fillRect(x - 2, cy, 22, ch, TFT_BLACK);

    // ── Draw glow as ghost characters ──
    for (int gr = 0; gr < 45; gr++) {
      uint8_t glow = g_clockGlow[c][gr];
      if (glow < 3) continue;
      int gy = gr * 16;
      if (gy + 16 <= cy || gy >= cy + ch) continue;
      uint16_t gch = g_glowChar[c][gr];
      if (!gch) continue;
      uint16_t gcol = ((glow * 12 / 255) << 11) | ((glow * 55 / 255) << 5);
      if (isKata) {
        drawKatakana(out, gch, x, gy, gcol);
      } else {
        out.setTextColor(gcol, TFT_BLACK);
        out.drawChar(gch, x, gy);
      }
    }

    for (int t = tl - 1; t >= 0; t--) {
      int py = headPx - t * ROW_H;
      if (py < 0 || py + ROW_H > SCREEN_H) continue;

      int ox = 0, oy = 0;
      uint16_t col = g_trailCol[t];
      bool inSeg = getRefraction(x, py, ox, oy, col);

      // Paint glow: only the head (newest rain) refreshes the glow
      // This creates a FIFO trail where top cells dim before bottom cells
      int row16 = py / 16;
      if (row16 >= 0 && row16 < 45 && inSeg && t == 0) {
        g_clockGlow[c][row16] = 255;
        g_glowChar[c][row16] = g_col[c].trail[t];
      }

      if (isKata) {
        drawKatakana(out, g_col[c].trail[t], x + ox, py + oy, col);
      } else {
        out.setTextColor(col, TFT_BLACK);
        out.drawChar(g_col[c].trail[t], x + ox, py + oy);
      }
    }
    g_col[c].prevY = g_col[c].y;
    if (c % 40 == 0) esp_task_wdt_reset();
  }
  out.endWrite();
  yield();
}

// ── SET button overlay ──
static void drawSetButton() {
  auto& d = M5.Display;
  const int bx = 1170, by = 670, bw = 90, bh = 34;
  d.fillRoundRect(bx, by, bw, bh, 4, d.color565(0, 12, 0));
  d.drawRoundRect(bx, by, bw, bh, 4, d.color565(0, 35, 0));
  d.setTextFont(1); d.setTextSize(1);
  d.setTextColor(d.color565(0, 100, 0));
  const char* lbl = "SET";
  int16_t tw = d.textWidth(lbl);
  d.drawString(lbl, bx + (bw - tw) / 2, by + (bh - 8) / 2);
}

// ── Touch handling ──
static uint32_t g_touchDebounce = 0;

// ── Idle state machine (forward declarations for handleTouch) ──
static uint32_t g_lastActivity = 0;
static int g_idleLevel = 0;
static int g_normalBright = 100;
static bool g_backlightOff = false;
static void onActivity();

static void handleTouch() {
  if (millis() - g_touchDebounce < 350) return;
  auto cnt = M5.Touch.getCount();
  if (cnt == 0) return;
  auto t = M5.Touch.getDetail();

  // Any touch = activity
  onActivity();

  // ── SET button (bottom-right corner) ──
  if (t.x >= 1170 && t.x < 1260 && t.y >= 670 && t.y < 704) {
    g_touchDebounce = millis();
    memset(g_clockGlow, 0, sizeof(g_clockGlow));
    memset(g_glowChar, 0, sizeof(g_glowChar));
    // Just clear display directly, like WiFi wizard does
    M5.Display.fillScreen(TFT_BLACK);
    runSetupMenu();
    // Clear display and canvas for clean transition back to clock
    M5.Display.fillScreen(TFT_BLACK);
    canvas.fillSprite(TFT_BLACK);
    updateClockFromNTP(g_h, g_m);
    reinitRain();
    rebuildTrailColors();
    g_lastActivity = millis();  // reset idle after returning
    return;
  }

  // ── Cycle character set ──
  g_touchDebounce = millis();
  onActivity();  // already called above, but keep debounce reset

  g_curSet = (g_curSet + 1) % CS_COUNT;
  reinitRain();
  memset(g_clockGlow, 0, sizeof(g_clockGlow));
  memset(g_glowChar, 0, sizeof(g_glowChar));
  M5.Display.fillScreen(TFT_BLACK);
  Serial.printf(">> CHARS: %s\n", g_charSets[g_curSet].name);
}

// ── Serial screenshot ──
static void doSerialSnap() {
  canvas.fillSprite(TFT_BLACK);
  drawRainOn(canvas);
  canvas.pushSprite(0, 0);

  uint16_t* fb = (uint16_t*)canvas.getBuffer();
  if (!fb) { Serial.println("NO_FB"); return; }

  int w = SCREEN_W, h = SCREEN_H;
  int rowSize24 = (w * 3 + 3) & ~3;
  int fileSize24 = 54 + rowSize24 * h;

  esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 180000, .trigger_panic = false };
  esp_task_wdt_init(&wdt_cfg);

  Serial.flush();
  Serial.begin(921600);
  delay(30);
  Serial.printf("BMP:%d\n", fileSize24);

  uint8_t hdr[54];
  memset(hdr, 0, 54);
  hdr[0] = 'B'; hdr[1] = 'M';
  uint32_t fs = fileSize24;
  memcpy(&hdr[2], &fs, 4);
  hdr[10] = 54;
  uint32_t dib = 40;
  memcpy(&hdr[14], &dib, 4);
  memcpy(&hdr[18], &w, 4);
  int32_t neg_h = -h;
  memcpy(&hdr[22], &neg_h, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  Serial.write(hdr, 54);

  uint8_t* rowBuf = (uint8_t*)malloc(rowSize24);
  if (!rowBuf) { Serial.begin(115200); return; }
  for (int y = 0; y < h; y++) {
    int off = y * w;
    int pos = 0;
    for (int x = 0; x < w; x++) {
      uint16_t px = fb[off + x];
      rowBuf[pos++] = (char)((px << 3) & 0xF8);
      rowBuf[pos++] = (char)((px >> 3) & 0xFC);
      rowBuf[pos++] = (char)((px >> 8) & 0xF8);
    }
    while (pos < rowSize24) rowBuf[pos++] = 0;
    Serial.write(rowBuf, rowSize24);
    if (y % 100 == 0) esp_task_wdt_reset();
  }
  free(rowBuf);
  Serial.printf("\nEND\n");
  Serial.begin(115200);
  delay(30);
  esp_task_wdt_config_t wdt_norm = { .timeout_ms = 5000, .trigger_panic = false };
  esp_task_wdt_init(&wdt_norm);
}

// ────────────────────────────────────────────────────────────
// SETUP MENU
// ────────────────────────────────────────────────────────────

// ── Brightness + Idle state machine ──
#define BL_GPIO 22
#define BL_CHANNEL 0
#define BL_FREQ 5000
#define BL_RES 12   // 12-bit = 0-4095

static void initBrightness() {
  ledc_timer_config_t tmr = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = (ledc_timer_bit_t)BL_RES,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = BL_FREQ,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&tmr);
  ledc_channel_config_t ch = {
    .gpio_num = BL_GPIO,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .hpoint = 0,
  };
  ledc_channel_config(&ch);
}

static int getBrightness() {
  Preferences p; p.begin("cyberclock", true);
  int v = p.getInt("bright", 100);
  p.end(); return constrain(v, 20, 100);
}
static void setBrightness(int v) {
  v = constrain(v, 20, 100);
  Preferences p; p.begin("cyberclock", false);
  p.putInt("bright", v); p.end();
  int duty = v * ((1 << BL_RES) - 1) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ── Idle state machine ──
// Levels: 0=normal, 1=dim(2min), 2=lowfps(3min), 3=backlight-off(10min)
static void onActivity() {
  g_lastActivity = millis();
  if (g_idleLevel > 0 || g_backlightOff) {
    // Restore from idle
    if (g_backlightOff) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, g_normalBright * 4095 / 100);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      g_backlightOff = false;
    }
    g_idleLevel = 0;
    setBrightness(g_normalBright);
  }
}

static int getIdleFps() {
  return (g_idleLevel >= 2) ? 8 : 30;  // idle at 8fps
}

// Menu item hit helper
static bool menuTap(int tx, int ty, int y, int h) {
  return tx >= 0 && tx < 1280 && ty >= y && ty < y + h;
}

// Draw the setup menu (called only when state changes)
// Uses direct DSI ops (d.xxx), NOT the canvas — same pattern as WiFi wizard
static void drawSetupMenu(int subState, int curItem, int brightPct) {
  auto& d = M5.Display;
  char csName[16];
  int twb;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, 1280, 48, d.color565(0, MG::TITLE_BG, 0));
  d.setTextFont(2); d.setTextSize(2); d.setTextColor(d.color565(0, MG::TITLE, 0));
  d.drawString("SETUP", 20, 10);

  if (subState == 0) {
    // ── Main menu ──
    const char* labels[] = { "WiFi Settings", "Character Set", "Brightness", "About", "Back" };
    const int ITEM_H = 56, LIST_TOP = 60;
    for (int i = 0; i < 5; i++) {
      int yy = LIST_TOP + i * (ITEM_H + 4);
      uint16_t bg = (i == curItem) ? d.color565(0, MG::SEL, 0) : TFT_BLACK;
      d.fillRect(20, yy, 1240, ITEM_H, bg);
      d.setTextFont(1); d.setTextSize(2);
      d.setTextColor(d.color565(0, MG::BRIGHT, 0));
      d.drawString(i == curItem ? "> " : "  ", 24, yy + 16);
      d.setTextColor(d.color565(0, MG::BODY, 0));
      d.drawString(labels[i], 56, yy + 16);
      if (i == 1) {
        snprintf(csName, sizeof(csName), "[ %s ]", g_charSets[g_curSet].name);
        d.setTextColor(d.color565(0, MG::DIM, 0));
        d.drawString(csName, 1160 - d.textWidth(csName), yy + 16);
      } else if (i == 2) {
        char b[8]; snprintf(b, sizeof(b), "%d%%", brightPct);
        d.setTextColor(d.color565(0, MG::DIM, 0));
        d.drawString(b, 1160 - d.textWidth(b), yy + 16);
      }
    }
  } else if (subState == 1) {
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
    d.drawString("Character Set", 40, 100);
    d.fillRoundRect(340, 260, 70, 56, 6, d.color565(0, MG::SEC, 0));
    d.drawRoundRect(340, 260, 70, 56, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextColor(d.color565(0, MG::DIM, 0)); d.setTextSize(3);
    d.drawString("<", 360, 268);
    snprintf(csName, sizeof(csName), "%s", g_charSets[g_curSet].name);
    d.setTextColor(d.color565(0, MG::BRIGHT, 0)); d.setTextSize(3);
    d.drawString(csName, (1280 - d.textWidth(csName)) / 2, 268);
    d.fillRoundRect(870, 260, 70, 56, 6, d.color565(0, MG::SEC, 0));
    d.drawRoundRect(870, 260, 70, 56, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextColor(d.color565(0, MG::DIM, 0)); d.setTextSize(3);
    d.drawString(">", 890, 268);
    d.fillRoundRect(540, 480, 200, 48, 6, d.color565(0, MG::CANCEL, 0));
    d.drawRoundRect(540, 480, 200, 48, 6, d.color565(0, MG::CANCEL_BDR, 0));
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::DIM, 0));
    twb = d.textWidth("Back");
    d.drawString("Back", 540 + (200 - twb) / 2, 480 + (48 - 16) / 2);
  } else if (subState == 2) {
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::BODY, 0));
    d.drawString("Brightness", 40, 100);
    int barX = 240, barY = 260, barW = 800, barH = 24;
    d.fillRoundRect(barX, barY, barW, barH, 4, d.color565(0, MG::LINE, 0));
    int fillW = (barW * brightPct) / 100;
    d.fillRoundRect(barX, barY, fillW, barH, 4, d.color565(0, MG::BTN_BDR, 0));
    char bp[8]; snprintf(bp, sizeof(bp), "%d%%", brightPct);
    d.setTextSize(3); d.setTextColor(d.color565(0, MG::BRIGHT, 0));
    d.drawString(bp, (1280 - d.textWidth(bp)) / 2, 310);
    d.fillRoundRect(340, 330, 80, 56, 6, d.color565(0, MG::SEC, 0));
    d.drawRoundRect(340, 330, 80, 56, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextSize(3); d.setTextColor(d.color565(0, MG::DIM, 0));
    d.drawString("-", 362, 338);
    d.fillRoundRect(860, 330, 80, 56, 6, d.color565(0, MG::SEC, 0));
    d.drawRoundRect(860, 330, 80, 56, 6, d.color565(0, MG::SEC_BDR, 0));
    d.setTextSize(3); d.setTextColor(d.color565(0, MG::DIM, 0));
    d.drawString("+", 882, 338);
    d.setTextSize(1); d.setTextColor(d.color565(0, MG::FAINT, 0));
    d.drawString("GPIO22 LEDC PWM 12-bit", 440, 420);
    d.fillRoundRect(540, 500, 200, 48, 6, d.color565(0, MG::CANCEL, 0));
    d.drawRoundRect(540, 500, 200, 48, 6, d.color565(0, MG::CANCEL_BDR, 0));
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::DIM, 0));
    twb = d.textWidth("Back");
    d.drawString("Back", 540 + (200 - twb) / 2, 500 + (48 - 16) / 2);
  } else if (subState == 3) {
    d.setTextFont(2); d.setTextSize(2);
    d.setTextColor(d.color565(0, MG::TITLE, 0));
    d.drawString("Matrix Rain Clock", 40, 100);
    d.setTextColor(d.color565(0, MG::DIM, 0)); d.drawString("v1.3.1", 40, 136);
    d.setTextColor(d.color565(0, MG::BODY, 0));
    d.drawString("M5Stack Tab5  |  ESP32-P4", 40, 180);
    d.setTextSize(1); d.setTextColor(d.color565(0, MG::FAINT, 0));
    d.drawString("\"There is no spoon.\"", 40, 240);
    d.drawString("https://github.com/andjiang0083/matrix-rain-tab5", 40, 270);
    d.fillRoundRect(540, 480, 200, 48, 6, d.color565(0, MG::CANCEL, 0));
    d.drawRoundRect(540, 480, 200, 48, 6, d.color565(0, MG::CANCEL_BDR, 0));
    d.setTextSize(2); d.setTextColor(d.color565(0, MG::DIM, 0));
    twb = d.textWidth("Back");
    d.drawString("Back", 540 + (200 - twb) / 2, 480 + (48 - 16) / 2);
  }
}

static bool getTouchReleased(int& tx, int& ty) {
  auto c = M5.Touch.getCount();
  if (c == 0) { tx = ty = -1; return false; }
  auto t = M5.Touch.getDetail();
  tx = t.x; ty = t.y;
  // Wait for release (capped at 3s watchdog-safe)
  uint32_t wt = millis();
  while (millis() - wt < 3000) {
    M5.update(); esp_task_wdt_reset();
    if (M5.Touch.getCount() == 0) break;
    delay(5);
  }
  return true;
}

void runSetupMenu() {
  auto& d = M5.Display;
  enum { ITEM_WIFI=0, ITEM_CHARSET, ITEM_BRIGHT, ITEM_ABOUT, ITEM_BACK };
  const int ITEM_H = 56;
  const int LIST_TOP = 60;
  int curItem = 0;
  int subState = 0; // 0=menu, 1=charset, 2=brightness, 3=about
  int brightPct = getBrightness();
  bool exitMenu = false;
  int lastSub = -1, lastCur = -1, lastBright = -1, lastCS = -1;
  bool dirty = true;

  while (!exitMenu) {
    M5.update(); esp_task_wdt_reset();

    // Redraw only if state changed (no flicker)
    if (dirty || subState != lastSub || curItem != lastCur
        || brightPct != lastBright
        || (subState == 1 && g_curSet != lastCS)) {
      drawSetupMenu(subState, curItem, brightPct);
      lastSub = subState; lastCur = curItem;
      lastBright = brightPct; lastCS = g_curSet;
      dirty = false;
    }

    // ── Touch ──
    int tx, ty;
    if (!getTouchReleased(tx, ty)) { delay(20); continue; }

    if (subState == 0) {
      for (int i = 0; i < 5; i++) {
        int yy = LIST_TOP + i * (ITEM_H + 4);
        if (menuTap(tx, ty, yy, ITEM_H)) {
          curItem = i;
          if (i == ITEM_WIFI) {
            dirty = true;
            reconnectWifi();
            brightPct = getBrightness();
          } else if (i == ITEM_CHARSET) { subState = 1; }
          else if (i == ITEM_BRIGHT)  { subState = 2; }
          else if (i == ITEM_ABOUT)   { subState = 3; }
          else if (i == ITEM_BACK)    { exitMenu = true; }
          break;
        }
      }
    } else if (subState == 1) {
      if (menuTap(tx, ty, 260, 56)) {
        if (tx > 340 && tx < 410) {  // < prev
          g_curSet = (g_curSet - 1 + CS_COUNT) % CS_COUNT;
          reinitRain();
          memset(g_clockGlow, 0, sizeof(g_clockGlow));
        } else if (tx > 870 && tx < 940) {  // > next
          g_curSet = (g_curSet + 1) % CS_COUNT;
          reinitRain();
          memset(g_clockGlow, 0, sizeof(g_clockGlow));
        }
      }
      if (menuTap(tx, ty, 480, 48) && tx > 540 && tx < 740) { subState = 0; dirty = true; }
    } else if (subState == 2) {
      if (menuTap(tx, ty, 330, 56)) {
        if (tx > 340 && tx < 420) {  // -
          brightPct = constrain(brightPct - 10, 20, 100);
          setBrightness(brightPct);
        } else if (tx > 860 && tx < 940) {  // +
          brightPct = constrain(brightPct + 10, 20, 100);
          setBrightness(brightPct);
        }
      }
      if (menuTap(tx, ty, 500, 48) && tx > 540 && tx < 740) { subState = 0; dirty = true; }
    } else if (subState == 3) {
      if (menuTap(tx, ty, 480, 48) && tx > 540 && tx < 740) { subState = 0; dirty = true; }
    }
    delay(10);
  }
}

// ── Setup ──
void setup() {
  Serial.begin(115200);
  delay(100);

  esp_task_wdt_config_t twdt_cfg = {
    .timeout_ms = 8000,
    .idle_core_mask = (1 << 0) | (1 << 1),
    .trigger_panic = true,
  };
  esp_task_wdt_init(&twdt_cfg);
  esp_task_wdt_add(NULL);

  Serial.println("=== MATRIX RAIN v1.3.1 (Boot NTP + RTC + confirm screen + WiFi-setup) ===");
  delay(500);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(SCREEN_ROTATION);
  M5.Display.setTextWrap(false);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);

  canvas.setPsram(true);
  if (!canvas.createSprite(SCREEN_W, SCREEN_H)) {
    Serial.println("Sprite ALLOC FAILED");
  } else {
    Serial.println("Sprite OK");
  }

  // ── WiFi + NTP setup ──
  if (!autoConnectAndSync()) {
    runWifiSetup();
  }
  updateClockFromNTP(g_h, g_m);

  rebuildTrailColors();
  initRain();
  initClockSegments();
  M5.Display.fillScreen(TFT_BLACK);
  // Init LEDC backlight PWM on GPIO 22 + apply saved brightness
  initBrightness();
  g_normalBright = getBrightness();
  setBrightness(g_normalBright);
  g_lastActivity = millis();

  // Disconnect WiFi after initial NTP sync, keep hosted stack alive
  esp_wifi_disconnect();

  Serial.printf("Bitmap font: %d chars loaded\n", KATA_N);
  Serial.println("Ready — tap screen to cycle character sets, S=screenshot");
}

// ── Loop ──
void loop() {
  uint32_t now = millis();
  uint32_t frameStart = now;
  static uint32_t fcp = 0, fct = 0;

  esp_task_wdt_reset();

  fcp++;
  if (now - fct >= 1000) { Serial.printf("FPS: %lu\n", fcp); fcp = 0; fct = now; }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'S' || c == 's') { Serial.println("SNAP..."); doSerialSnap(); }
  }

  // ── Idle state machine ──
  uint32_t idleMs = now - g_lastActivity;
  if (!g_backlightOff) {
    if (idleMs > 600000 && g_idleLevel < 3) {
      // Level 3: backlight off (10 min)
      g_idleLevel = 3;
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      g_backlightOff = true;
    } else if (idleMs > 180000 && g_idleLevel < 2) {
      // Level 2: low FPS (3 min)
      g_idleLevel = 2;
    } else if (idleMs > 120000 && g_idleLevel < 1) {
      // Level 1: dim brightness (2 min)
      g_idleLevel = 1;
      setBrightness(25);
    }
  }

  // ── Frame rate limiting (dynamic based on idle) ──
  int fps = getIdleFps();
  uint32_t frameMs = 1000 / fps;
  static uint32_t lastFrame = 0;
  if (now - lastFrame < frameMs) {
    // Skip this frame, just do minimal housekeeping
    M5.update();
    handleTouch();
    uint32_t ft = millis() - frameStart;
    if (ft < frameMs) delay(frameMs - ft);
    return;
  }
  lastFrame = now;

  // ── Read RTC time (battery-backed, no WiFi needed) ──
  static uint32_t lastTimeRead = 0;
  if (now - lastTimeRead > 60000) {  // read RTC every 60s (cheap)
    updateClockFromNTP(g_h, g_m);
    lastTimeRead = now;
  }

  M5.update();
  handleTouch();
  frameStart = millis();

  updateRain();
  drawRainOn(M5.Display);
  if (!g_backlightOff) drawSetButton();

  uint32_t ft = millis() - frameStart;
  if (ft > 3000) {
    Serial.printf("TIMEOUT %lums\n", ft);
    delay(10);
    esp_restart();
  }

  if (ft < FRAME_MS) delay(FRAME_MS - ft);
  delay(1);
}
