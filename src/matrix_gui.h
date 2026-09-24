// ────────────────────────────────────────────────────────────
// MATRIX GUI — Unified color palette & helpers for Cyber Clock
// All screens share this palette: green-on-black, no red/blue.
// ────────────────────────────────────────────────────────────
#pragma once

// ── Green Component Palette ──
// Usage:  d.fillRect(x, y, w, h, d.color565(0, MG::XXXXX, 0));
namespace MG {
  // Backgrounds
  inline constexpr uint8_t BG       = 0;    // pure black
  inline constexpr uint8_t TITLE_BG = 20;   // title bar
  inline constexpr uint8_t PANEL    = 8;    // panel/card bg
  inline constexpr uint8_t FIELD    = 12;   // input field bg

  // Text
  inline constexpr uint8_t BRIGHT   = 255;  // brightest
  inline constexpr uint8_t TITLE    = 200;  // title text
  inline constexpr uint8_t BODY     = 180;  // body text
  inline constexpr uint8_t DIM      = 60;   // disabled/hint
  inline constexpr uint8_t FAINT    = 30;   // almost invisible
  inline constexpr uint8_t WARN_G   = 160;  // warning text (warm green)

  // Buttons — primary (affirmative: Connect / Done / Confirm)
  inline constexpr uint8_t BTN      = 55;   // fill
  inline constexpr uint8_t BTN_BDR  = 120;  // border

  // Buttons — secondary (toggle: Scan / Retry)
  inline constexpr uint8_t SEC      = 35;
  inline constexpr uint8_t SEC_BDR  = 70;

  // Buttons — cancel/back (intentionally low-contrast)
  inline constexpr uint8_t CANCEL      = 18;
  inline constexpr uint8_t CANCEL_BDR  = 35;

  // Selection / highlight
  inline constexpr uint8_t SEL      = 80;   // selected item bg
  inline constexpr uint8_t HOVER    = 40;   // alternate row

  // Warning / error (still green, just yellow-green)
  inline constexpr uint8_t WARN_R   = 120;  // R component for warm tone
  inline constexpr uint8_t WARN_BG  = 40;   // warning bg fill

  // Lines / borders
  inline constexpr uint8_t LINE     = 25;

  // Keyboard special keys
  inline constexpr uint8_t KB_MODE_R = 0;    // mode toggle r (teal tone)
  inline constexpr uint8_t KB_MODE_G = 60;   // mode toggle g
  inline constexpr uint8_t KB_MODE_B = 30;   // mode toggle b
  inline constexpr uint8_t KB_SHIFT  = 80;   // shift key active
  inline constexpr uint8_t KB_DEL_R  = 60;   // DEL key r

  // SSID replacement □ color
  inline constexpr uint8_t REPLACE  = 100;

  // Accent (for time display)
  inline constexpr uint8_t ACCENT_R = 50;
  inline constexpr uint8_t ACCENT_G = 255;
  inline constexpr uint8_t ACCENT_B = 50;
}

// ── SSID sanitizer ──
// Replace non-ASCII (CJK) bytes with spaces, recording character positions
// for rendering □ boxes. Returns replacement count.
static inline int sanitizeSSID(char* out, int outLen,
                               const char* in, int inLen,
                               int* replPos = nullptr, int maxRepl = 0) {
  int oi = 0, ri = 0;
  for (int ii = 0; ii < inLen && oi < outLen - 1; ii++) {
    unsigned char c = (unsigned char)in[ii];
    if (c >= 0x20 && c <= 0x7E) {
      out[oi++] = (char)c;
    } else {
      // Skip UTF-8 continuation bytes
      if      ((c & 0xE0) == 0xC0) ii += 1;   // 2-byte
      else if ((c & 0xF0) == 0xE0) ii += 2;   // 3-byte (CJK)
      else if ((c & 0xF8) == 0xF0) ii += 3;   // 4-byte
      if (replPos && ri < maxRepl) replPos[ri++] = oi;
      out[oi++] = ' ';  // placeholder space
    }
  }
  out[oi] = 0;
  return ri;
}
