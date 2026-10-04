// ────────────────────────────────────────────────────────────
// UI KIT — one visual language for every settings screen.
//
// Every rule here exists because it was a real defect on the panel:
//
//   * One font family: GLCD 5x7 (M5GFX Font0), integer scale only. The old
//     screens mixed Font0@2 with Font2@2 and Font2@3, and Font2 is a narrow
//     proportional face — same nominal height, completely different texture.
//   * Labels are centred from their REAL glyph metrics. The old code assumed
//     a 16 px line box while drawing 32 px text, which pushed 12 buttons
//     down by 8 px (and 26 px on the NTP-failure button).
//   * One back control, one place, one wording: below the title bar,
//     top-left. It used to live in five different spots and was called
//     Back / Cancel / Done.
//   * Four button styles, five text sizes, no ad-hoc colours. Ghost buttons
//     used FAINT-on-CANCEL, which measured 1.09:1 — invisible on a real panel.
// ────────────────────────────────────────────────────────────
#pragma once
#include <M5Unified.h>
#include "matrix_gui.h"

namespace UI {

  // ── type scale (GLCD size → 8 px per unit) ──
  constexpr uint8_t T_NOTE  = 1;   // 8 px
  constexpr uint8_t T_SMALL = 2;   // 16 px
  constexpr uint8_t T_BODY  = 3;   // 24 px  buttons / rows / key caps
  constexpr uint8_t T_BIG   = 4;   // 32 px  menu items / big numbers
  constexpr uint8_t T_CLOCK = 5;   // 40 px  time display
  constexpr uint8_t T_TITLE = 3;   // 24 px  page title

  // ── geometry ──
  constexpr int BAR_H  = 56;                    // title bar
  constexpr int BACK_X = 24, BACK_Y = 84, BACK_H = 44;
  constexpr int BACK_HIT_W = 280;               // hit box, covers "< BACK" … "< CANCEL"
  constexpr int ACT_Y = 632, ACT_H = 56;        // bottom action row
  constexpr int ACT_X  = 24;                    // bottom-left action button
  constexpr int ACT_XR = 1280 - 24 - 240;       // bottom-right action button
  constexpr int ACT_W  = 240;
  constexpr int RAD = 8;

  // ── settings list (centred card stack) ──
  constexpr int CARD_X = 80, CARD_W = 1120, CARD_H = 88, CARD_GAP = 10;
  inline int cardY(int i, int n) {
    int total = n * CARD_H + (n - 1) * CARD_GAP;
    return BAR_H + (720 - BAR_H - total) / 2 + i * (CARD_H + CARD_GAP);
  }

  inline uint16_t g(M5GFX& d, uint8_t v) { return d.color565(0, v, 0); }
  inline int cellH(uint8_t size) { return 8 * size; }
  inline void font(M5GFX& d, uint8_t size) { d.setTextFont(1); d.setTextSize(size); }

  // ── text ──
  inline void center(M5GFX& d, uint8_t size, const char* s, int x, int y, int w, int h, uint8_t col) {
    font(d, size); d.setTextColor(g(d, col));
    d.drawString(s, x + (w - d.textWidth(s)) / 2, y + (h - cellH(size)) / 2);
  }
  inline void left(M5GFX& d, uint8_t size, const char* s, int x, int y, uint8_t col) {
    font(d, size); d.setTextColor(g(d, col)); d.drawString(s, x, y);
  }
  inline void right(M5GFX& d, uint8_t size, const char* s, int xRight, int y, uint8_t col) {
    font(d, size); d.setTextColor(g(d, col)); d.drawString(s, xRight - d.textWidth(s), y);
  }
  inline void centered(M5GFX& d, uint8_t size, const char* s, int y, uint8_t col) {
    font(d, size); d.setTextColor(g(d, col)); d.drawString(s, (1280 - d.textWidth(s)) / 2, y);
  }

  // ── buttons ──
  enum BtnStyle { PRIMARY, SECONDARY, GHOST, WARN, KEY };
  inline void button(M5GFX& d, int x, int y, int w, int h, const char* label,
                     BtnStyle st = SECONDARY, uint8_t size = T_BODY) {
    struct S { uint8_t fill, bdr, txt; };
    static const S table[5] = {{MG::BTN,     MG::BTN_BDR, MG::BRIGHT},   // PRIMARY
                               {MG::SEC,     MG::SEC_BDR, MG::BODY},     // SECONDARY
                               {MG::CANCEL,  MG::CANCEL_BDR, MG::BODY},  // GHOST
                               {MG::WARN_BG, MG::SEC_BDR, MG::WARN_G},   // WARN
                               {MG::HOVER,   MG::LINE,    MG::BODY}};    // KEY
    d.fillRoundRect(x, y, w, h, RAD, g(d, table[st].fill));
    d.drawRoundRect(x, y, w, h, RAD, g(d, table[st].bdr));
    center(d, size, label, x, y, w, h, table[st].txt);
  }

  // ── scrollable list row (scan results / pickers) ──
  // Geometry lives here, not at the call sites: the drawing code and the touch
  // hit test must never drift apart (a row that draws at 42 px but hits at 44
  // selects the wrong network, and that is invisible until a real finger).
  constexpr int LIST_X = 24, LIST_W = 1232, LIST_TOP = BAR_H + 8;
  constexpr int ROW_H = 42, ROW_GAP = 4, ROW_PITCH = ROW_H + ROW_GAP;
  constexpr int LIST_MAX_ROWS = 12;                  // 64 + 12*46 = 616 < ACT_Y(632) ✓
  inline int rowY(int i) { return LIST_TOP + i * ROW_PITCH; }
  inline void rowBg(M5GFX& d, int y, bool selected) {
    d.fillRoundRect(LIST_X, y, LIST_W, ROW_H, 8, g(d, selected ? MG::SEL : MG::PANEL));
    if (selected) d.fillRect(LIST_X + 10, y + 8, 6, ROW_H - 16, g(d, MG::BRIGHT));
  }

  // ── centred dim note ──
  inline void hint(M5GFX& d, const char* s, int y, uint8_t col = MG::DIM, uint8_t size = T_SMALL) {
    centered(d, size, s, y, col);
  }

  // ── chrome ──
  inline void bar(M5GFX& d, const char* title, const char* step = nullptr) {
    d.fillRect(0, 0, 1280, BAR_H, g(d, MG::TITLE_BG));
    left(d, T_TITLE, title, 24, (BAR_H - cellH(T_TITLE)) / 2, MG::TITLE);
    if (step) right(d, T_SMALL, step, 1256, (BAR_H - cellH(T_SMALL)) / 2, MG::DIM);
  }

  // The one back control. Width follows the label, but the hit box does not —
  // it is a fixed generous target so a tap can never fall between two screens.
  inline void back(M5GFX& d, const char* label = "< BACK") {
    font(d, T_BODY);
    int w = d.textWidth(label) + 56;
    button(d, BACK_X, BACK_Y, w, BACK_H, label, GHOST);
  }
  inline bool backHit(int tx, int ty) {
    return tx >= BACK_X - 8 && tx < BACK_X + BACK_HIT_W && ty >= BACK_Y - 8 && ty < BACK_Y + BACK_H + 8;
  }

  // ── settings card row ──
  inline void card(M5GFX& d, int x, int y, int w, int h, const char* label,
                   const char* value = nullptr, bool selected = false) {
    d.fillRoundRect(x, y, w, h, 10, g(d, selected ? MG::SEL : MG::PANEL));
    d.drawRoundRect(x, y, w, h, 10, g(d, selected ? MG::BTN_BDR : MG::LINE));
    if (selected) d.fillRect(x + 12, y + 14, 6, h - 28, g(d, MG::BRIGHT));
    left(d, T_BIG, label, x + 40, y + (h - cellH(T_BIG)) / 2,
         selected ? MG::BRIGHT : MG::BODY);
    int rx = x + w - 40;
    if (value) {
      font(d, T_BODY);
      int vw = d.textWidth(value);
      left(d, T_BODY, value, rx - 24 - vw, y + (h - cellH(T_BODY)) / 2,
           selected ? MG::BRIGHT : MG::BODY);   // was DIM-on-SEL = 1.30:1
      rx -= 24 + vw + 24;
    }
    font(d, T_BODY);
    d.setTextColor(g(d, selected ? MG::BRIGHT : MG::DIM));
    d.drawString(">", rx - d.textWidth(">"), y + (h - cellH(T_BODY)) / 2);
  }

  // ── slider bar (brightness) ──
  inline void slider(M5GFX& d, int x, int y, int w, int h, int pct) {
    d.fillRoundRect(x, y, w, h, 6, g(d, MG::LINE));
    int fw = (w * (pct < 0 ? 0 : pct > 100 ? 100 : pct)) / 100;
    if (fw > 6) d.fillRoundRect(x, y, fw, h, 6, g(d, MG::BTN_BDR));
    else if (fw > 0) d.fillRect(x, y, fw, h, g(d, MG::BTN_BDR));
  }

  inline uint16_t accent(M5GFX& d) { return d.color565(MG::ACCENT_R, MG::ACCENT_G, MG::ACCENT_B); }
  inline uint16_t warn(M5GFX& d)   { return d.color565(MG::WARN_R, MG::WARN_G, 0); }

  inline void vspace() {}   // (kept for callers that group chrome + content)
}
