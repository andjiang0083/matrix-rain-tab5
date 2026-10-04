#!/usr/bin/env python3
# ────────────────────────────────────────────────────────────
# ui_mock.py — PC re-render of the Tab5 settings UI (review harness)
#
# Purpose: draw every settings screen EXACTLY as the firmware does, so the
# review images are ground truth. Glyphs come from the M5GFX sources in
# .pio/libdeps/tab5/M5GFX/src — no system fonts, no approximations:
#   font 1 -> fonts::Font0  (GLCDfont, 6x8 cell, 5 data columns, col-major,
#                            bit j of a column byte = row j, LSB = top)
#   font 2 -> fonts::Font2  (BMPfont, 16px, width table widtbl_f16,
#                            one byte per row, MSB = leftmost column,
#                            last column is a trimmed margin)
# Color: M5GFX color565(r,g,b) = (r>>3)<<11 | (g>>2)<<5 | b>>3.
#
# Layout constants are copied 1:1 from src/main.cpp / src/wifi_setup.cpp with
# the source line noted; palette and TZ table are PARSED from the sources so
# they can never drift. This script is an evaluation tool, not a deliverable:
# when the firmware changes, this must change with it.
# ────────────────────────────────────────────────────────────
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX  = os.path.join(ROOT, '.pio/libdeps/tab5/M5GFX/src')
OUT  = os.environ.get('UI_MOCK_OUT', os.path.join(ROOT, '.uimock'))

from PIL import Image, ImageDraw

# ─────────────────────────── color ───────────────────────────
def color565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def rgb888(c):
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

TFT_BLACK = color565(0, 0, 0)

# ─────────────────── palette (parsed from matrix_gui.h) ───────────────────
def load_palette():
    src = open(os.path.join(ROOT, 'src/matrix_gui.h')).read()
    mg = {m.group(1): int(m.group(2))
          for m in re.finditer(r'inline constexpr uint8_t (\w+)\s*=\s*(\d+);', src)}
    for k in ('BG', 'TITLE_BG', 'PANEL', 'FIELD', 'BRIGHT', 'TITLE', 'BODY', 'DIM',
              'FAINT', 'BTN', 'BTN_BDR', 'SEC', 'SEC_BDR', 'CANCEL', 'CANCEL_BDR',
              'SEL', 'HOVER', 'LINE', 'ACCENT_R', 'ACCENT_G', 'ACCENT_B', 'REPLACE'):
        assert k in mg, 'palette key missing in matrix_gui.h: ' + k
    return mg

MG = load_palette()

# ─────────────────── fonts (parsed from M5GFX) ───────────────────
def strip_inactive(text):
    """Keep the #ifdef branch, drop the #else branch (M5GFX Font16.h)."""
    out, active = [], True
    for line in text.splitlines():
        t = line.strip()
        if t.startswith('#ifdef') or t.startswith('#if '): active = True; continue
        if t.startswith('#else'):  active = False; continue
        if t.startswith('#endif'): active = True; continue
        if active: out.append(line)
    return '\n'.join(out)

class Font0:
    """GLCDfont, fontdata[1]. Cell 6x8; 5 data columns per glyph."""
    name, width, height, xadv = 'Font0', 6, 8, 6
    def load(self):
        s = open(os.path.join(GFX, 'lgfx/Fonts/glcdfont.h')).read()
        i = s.index('{', s.index('static const unsigned char font[]'))
        j = s.index('};', i)
        b = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', s[i + 1:j])]
        assert len(b) == 256 * 5, len(b)
        self.codes = {c: b[c * 5:c * 5 + 5] for c in range(256)}

class Font2:
    """BMPfont, fontdata[2]. 16px tall, per-char width table."""
    name, height = 'Font2', 16
    def load(self):
        s = strip_inactive(open(os.path.join(GFX, 'lgfx/Fonts/Font16.h')).read())
        wt = s.index('widtbl_f16[96]')
        block = s[s.index('{', wt): s.index('};', wt)]
        self.widths = [int(v) for v in re.findall(r'\d+', re.sub(r'//.*', '', block))]
        assert len(self.widths) == 96, len(self.widths)
        self.codes = {}
        for m in re.finditer(r'chr_f16_([0-9A-F]{2})\[(\d+)\][^;{]*\{(.*?)\};', s, re.S):
            code, n = int(m.group(1), 16), int(m.group(2))
            data = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', m.group(3))]
            assert len(data) in (16, 32), (m.group(1), len(data))
            self.codes[code] = data
        assert len(self.codes) == 96, len(self.codes)
    def advance(self, c):
        i = c - 0x20
        return self.widths[i] if 0 <= i < 96 else self.widths[0]

FONT0, FONT2 = Font0(), Font2()
FONT0.load(); FONT2.load()
# ─────────────────────────── tiny GFX surface ───────────────────────────
class Screen:
    W, H = 1280, 720
    def __init__(self):
        self.img = Image.new('RGB', (self.W, self.H), rgb888(TFT_BLACK))
        self.d = ImageDraw.Draw(self.img)
        self.font, self.size = FONT0, 1
        self.fg = self.bg = rgb888(TFT_BLACK)

    # --- state, mirrors LGFXBase ---
    def setTextFont(self, f): self.font = FONT0 if f == 1 else FONT2
    def setTextSize(self, s): self.size = s
    def setTextColor(self, fg, bg=None): self.fg = rgb888(fg) if isinstance(fg, int) else fg
    def color565(self, r, g, b): return color565(r, g, b)

    def textWidth(self, s):
        if self.font is FONT0:
            return FONT0.xadv * self.size * len(s)
        return sum(FONT2.advance(ord(ch)) for ch in s) * self.size

    # --- primitives (all clipped to the panel) ---
    def _clip(self, x, y, w, h):
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(self.W, x + w), min(self.H, y + h)
        return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None

    def fillScreen(self, c):
        self.d.rectangle([0, 0, self.W - 1, self.H - 1], fill=rgb888(c))

    def fillRect(self, x, y, w, h, c):
        r = self._clip(x, y, w, h)
        if r: self.d.rectangle([r[0], r[1], r[2] - 1, r[3] - 1], fill=rgb888(c))

    def fillRoundRect(self, x, y, w, h, rad, c):
        if w <= 0 or h <= 0: return
        lay = Image.new('RGB', (w, h), rgb888(c))
        m = Image.new('L', (w, h), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], rad, fill=255)
        self.img.paste(lay, (x, y), m)   # PIL clips the paste box at the panel edge

    def drawRect(self, x, y, w, h, c):
        r = self._clip(x, y, w, h)
        if r: self.d.rectangle([r[0], r[1], r[2] - 1, r[3] - 1], outline=rgb888(c))

    def drawRoundRect(self, x, y, w, h, rad, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], rad, outline=rgb888(c))

    # --- text ---
    def drawChar(self, c, x, y):
        s = self.size
        if self.font is FONT0:
            g = FONT0.codes.get(c & 0xFF)
            if not g: return
            for col in range(5):                      # datawidth = 5
                byte = g[col]
                for row in range(8):
                    if byte & (1 << row):             # LSB = top row
                        self._px(x + col * s, y + row * s, s)
        else:
            i = c - 0x20
            if not (0 <= i < 96): return
            w = FONT2.widths[i]
            data = FONT2.codes.get(c)
            if data is None: return
            bs = (w + 6) >> 3
            for row in range(16):
                for col in range(w - 1):              # margin = 1 (last column trimmed)
                    if data[row * bs + (col >> 3)] & (0x80 >> (col & 7)):
                        self._px(x + col * s, y + row * s, s)

    def _px(self, x, y, s):
        r = self._clip(x, y, s, s)
        if r: self.d.rectangle([r[0], r[1], r[2] - 1, r[3] - 1], fill=self.fg)

    def drawString(self, string, x, y):
        """textdatum = top_left (the firmware default)."""
        cx = x
        for ch in string:
            c = ord(ch)
            if c < 0x20: continue
            self.drawChar(c, cx, y)
            cx += (FONT0.xadv if self.font is FONT0 else FONT2.advance(c)) * self.size
        return cx - x

# ─────────────────── parsed firmware tables ───────────────────
def load_tz_list():
    src = open(os.path.join(ROOT, 'src/wifi_setup.cpp')).read()
    blk = src[src.index('static const struct'):src.index('static const int TZ_COUNT')]
    out = [(m.group(1), int(m.group(2)))
           for m in re.finditer('[{][ ]*"([A-Z0-9:+-]+)"[ ]*,[ ]*(-?[0-9]+)[ ]*[}]', blk)]
    assert len(out) == 28, len(out)   # TZ_COUNT = sizeof(TZ_LIST)/sizeof(TZ_LIST[0])
    return out

TZ_LIST = load_tz_list()

# wifi_setup.cpp: K_SZ / K_GAP / K_BASE_Y / K_ALPHA  (lines 174-179)
K_SZ, K_GAP, K_BASE_Y = 108, 6, 152
K_ALPHA = ["QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"]
K_NUM = ["1234567890", "-_@#.$%+=", ",/:;\"'!?()"]

COL = lambda v: color565(0, v, 0)

# ═══════════════════════════ screens ═══════════════════════════
def draw_title_bar(d, text, step=None):
    """wifi_setup.cpp: drawTitle / drawStepTitle (lines 45-63)"""
    d.fillRect(0, 0, 1280, 48, COL(MG['TITLE_BG']))
    d.setTextFont(2); d.setTextSize(2)
    if step is None:
        d.setTextColor(COL(MG['TITLE'])); d.drawString(text, 20, 10)
    else:
        sw = d.textWidth(step)
        d.setTextColor(COL(MG['DIM']));  d.drawString(step, 20, 10)
        d.setTextColor(COL(MG['TITLE'])); d.drawString(text, 28 + sw, 10)

# ── main.cpp drawSetupMenu (572-664) ──
MENU_LABELS = ["WiFi Settings", "Character Set", "Brightness", "About", "Back"]

def screen_setup_menu(curItem=0, cs="FULL", bright=50):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.fillRect(0, 0, 1280, 48, COL(MG['TITLE_BG']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['TITLE']))
    d.drawString("SETUP", 20, 10)                      # main.cpp:579
    ITEM_H, LIST_TOP = 56, 60                          # main.cpp:584
    for i in range(5):
        yy = LIST_TOP + i * (ITEM_H + 4)
        bg = COL(MG['SEL']) if i == curItem else TFT_BLACK
        d.fillRect(20, yy, 1240, ITEM_H, bg)
        d.setTextFont(1); d.setTextSize(2)
        d.setTextColor(COL(MG['BRIGHT']))
        d.drawString("> " if i == curItem else "  ", 24, yy + 16)
        d.setTextColor(COL(MG['BODY']))
        d.drawString(MENU_LABELS[i], 56, yy + 16)
        if i == 1:
            s = "[ %s ]" % cs
            d.setTextColor(COL(MG['DIM']))
            d.drawString(s, 1160 - d.textWidth(s), yy + 16)
        elif i == 2:
            s = "%d%%" % bright
            d.setTextColor(COL(MG['DIM']))
            d.drawString(s, 1160 - d.textWidth(s), yy + 16)
    return d

def screen_setup_charset(cs="KATAKANA"):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.setTextSize(2); d.setTextColor(COL(MG['BODY']))   # font is still Font2 here
    d.drawString("Character Set", 40, 100)              # main.cpp:605-606
    d.fillRoundRect(340, 260, 70, 56, 6, COL(MG['SEC']))
    d.drawRoundRect(340, 260, 70, 56, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['DIM'])); d.setTextSize(3)
    d.drawString("<", 360, 268)
    d.setTextColor(COL(MG['BRIGHT'])); d.setTextSize(3)
    d.drawString(cs, (1280 - d.textWidth(cs)) // 2, 268)
    d.fillRoundRect(870, 260, 70, 56, 6, COL(MG['SEC']))
    d.drawRoundRect(870, 260, 70, 56, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['DIM'])); d.setTextSize(3)
    d.drawString(">", 890, 268)
    d.fillRoundRect(540, 480, 200, 48, 6, COL(MG['CANCEL']))
    d.drawRoundRect(540, 480, 200, 48, 6, COL(MG['CANCEL_BDR']))
    d.setTextSize(2); d.setTextColor(COL(MG['DIM']))
    twb = d.textWidth("Back")
    d.drawString("Back", 540 + (200 - twb) // 2, 480 + (48 - 16) // 2)
    return d

def screen_setup_bright(bright=50):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.setTextSize(2); d.setTextColor(COL(MG['BODY']))
    d.drawString("Brightness", 40, 100)                 # main.cpp:624-625
    barX, barY, barW, barH = 240, 260, 800, 24
    d.fillRoundRect(barX, barY, barW, barH, 4, COL(MG['LINE']))
    d.fillRoundRect(barX, barY, barW * bright // 100, barH, 4, COL(MG['BTN_BDR']))
    bp = "%d%%" % bright
    d.setTextSize(3); d.setTextColor(COL(MG['BRIGHT']))
    d.drawString(bp, (1280 - d.textWidth(bp)) // 2, 310)
    d.fillRoundRect(340, 330, 80, 56, 6, COL(MG['SEC']))
    d.drawRoundRect(340, 330, 80, 56, 6, COL(MG['SEC_BDR']))
    d.setTextSize(3); d.setTextColor(COL(MG['DIM']))
    d.drawString("-", 362, 338)
    d.fillRoundRect(860, 330, 80, 56, 6, COL(MG['SEC']))
    d.drawRoundRect(860, 330, 80, 56, 6, COL(MG['SEC_BDR']))
    d.setTextSize(3); d.setTextColor(COL(MG['DIM']))
    d.drawString("+", 882, 338)
    d.setTextSize(1); d.setTextColor(COL(MG['FAINT']))
    d.drawString("GPIO22 LEDC PWM 12-bit", 440, 420)     # main.cpp:642
    d.fillRoundRect(540, 500, 200, 48, 6, COL(MG['CANCEL']))
    d.drawRoundRect(540, 500, 200, 48, 6, COL(MG['CANCEL_BDR']))
    d.setTextSize(2); d.setTextColor(COL(MG['DIM']))
    twb = d.textWidth("Back")
    d.drawString("Back", 540 + (200 - twb) // 2, 500 + (48 - 16) // 2)
    return d

def screen_setup_about(version="1.3.1"):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.setTextFont(2); d.setTextSize(2)
    d.setTextColor(COL(MG['TITLE']))
    d.drawString("Matrix Rain Clock", 40, 100)          # main.cpp:651
    d.setTextColor(COL(MG['DIM'])); d.drawString("v" + version, 40, 136)
    d.setTextColor(COL(MG['BODY']))
    d.drawString("M5Stack Tab5  |  ESP32-P4", 40, 180)
    d.setTextSize(1); d.setTextColor(COL(MG['FAINT']))
    d.drawString("\"There is no spoon.\"", 40, 240)
    d.drawString("https://github.com/andjiang0083/matrix-rain-tab5", 40, 270)
    d.fillRoundRect(540, 480, 200, 48, 6, COL(MG['CANCEL']))
    d.drawRoundRect(540, 480, 200, 48, 6, COL(MG['CANCEL_BDR']))
    d.setTextSize(2); d.setTextColor(COL(MG['DIM']))
    twb = d.textWidth("Back")
    d.drawString("Back", 540 + (200 - twb) // 2, 480 + (48 - 16) // 2)
    return d

# ── wifi_setup.cpp ──
SSIDS = [("CMCC-8fZq", -48, False), ("TP-LINK_5G_2.4", -61, False),
         ("Xiaomi_AX3000", -70, False), ("CoffeeBar-Guest", -78, True),
         ("HUAWEI-2.4G", -84, False), ("★我的WiFi★", -66, False),
         ("dlink-3F20", -88, False), ("MERCURY_2.4G", -55, False)]

def screen_wifi_scan():
    d = Screen()
    d.fillScreen(TFT_BLACK)
    draw_title_bar(d, "Scanning...")
    d.setTextSize(2); d.setTextColor(COL(MG['BODY']))
    d.drawString("Scanning...", 540, 300)               # wifi_setup.cpp:591
    return d

def screen_wifi_list(sel=2):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    draw_title_bar(d, "Select Network", step="1/4")
    d.fillRoundRect(40, 680, 160, 36, 6, COL(MG['SEC']))       # 105-108
    d.drawRoundRect(40, 680, 160, 36, 6, COL(MG['SEC_BDR']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['DIM']))
    d.drawString("[Scan]", 60, 688)
    d.fillRoundRect(1080, 680, 160, 36, 6, COL(MG['CANCEL']))  # 111-114
    d.drawRoundRect(1080, 680, 160, 36, 6, COL(MG['CANCEL_BDR']))
    d.setTextColor(COL(MG['FAINT']))
    d.drawString("Back", 1120, 688)
    yy = 56
    for i, (ssid, rssi, is_open) in enumerate(SSIDS):
        bg = COL(MG['SEL']) if i == sel else TFT_BLACK
        d.fillRect(10, yy, 1260, 40, bg)
        d.setTextFont(1); d.setTextSize(2); d.setTextColor(COL(MG['BRIGHT']))
        d.drawString(">" if i == sel else " ", 10, yy + 8)
        d.setTextColor(COL(MG['BODY']))
        start = 30
        for ch in ssid:
            if ord(ch) < 128:
                d.drawString(ch, start, yy + 8)
                start += d.textWidth(ch)
            else:                                   # □ box, main radio: matrix_gui.h
                d.drawRect(start, yy + 10, 10, 14, COL(MG['REPLACE']))
                start += d.textWidth(" ")
        bars = max(1, min(5, (rssi + 90) * 4 // 60 + 1))
        for b in range(bars):
            d.fillRect(1060 + b * 10, yy + 24 - b * 5, 8, b * 5 + 4,
                       COL(MG['DIM'] + (MG['BRIGHT'] - MG['DIM']) * b // 4))
        for b in range(bars, 5):
            d.fillRect(1060 + b * 10, yy + 24 - b * 5, 8, b * 5 + 4, COL(MG['LINE']))
        if is_open:
            d.setTextColor(COL(MG['DIM']))          # font/size still Font1 @2 (line 123)
            d.drawString("OPEN", 1120, yy + 8)
        yy += 44
    return d

def screen_wifi_keyboard(pw="", shift=False, alpha=True):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    draw_title_bar(d, "Enter Password", step="2/4")
    d.setTextFont(1); d.setTextSize(2); d.setTextColor(COL(MG['DIM']))
    d.drawString("CMCC-8fZq", 20, 56)                    # wifi_setup.cpp:185-186
    d.fillRect(18, 82, 1244, 56, COL(MG['FIELD']))
    d.drawRect(18, 82, 1244, 56, COL(MG['LINE']))
    d.setTextFont(1); d.setTextSize(2)
    if not pw:
        d.setTextColor(COL(MG['DIM'])); d.drawString("tap keys above", 30, 98)
    else:
        d.setTextColor(COL(MG['BODY'])); d.drawString(pw, 24, 98)
    rows, lens = (K_ALPHA, [10, 9, 7]) if alpha else (K_NUM, [10, 10, 10])
    for r in range(3):
        yy = K_BASE_Y + r * (K_SZ + K_GAP)
        totalW = lens[r] * K_SZ + (lens[r] - 1) * K_GAP
        xx = (1280 - totalW) // 2
        for k in range(lens[r]):
            ch = rows[r][k]
            if alpha and shift: ch = ch.upper()
            d.fillRoundRect(xx, yy, K_SZ, K_SZ, 6, COL(MG['HOVER']))
            d.drawRoundRect(xx, yy, K_SZ, K_SZ, 6, COL(MG['LINE']))
            d.setTextColor(COL(MG['BODY'])); d.setTextSize(2); d.setTextFont(1)
            lbl = ch
            tw = d.textWidth(lbl)
            d.drawString(lbl, xx + (K_SZ - tw) // 2, yy + (K_SZ - 20) // 2)
            xx += K_SZ + K_GAP
    by = K_BASE_Y + 3 * (K_SZ + K_GAP)
    lx = 20
    shiftBg = COL(MG['KB_SHIFT']) if shift else COL(MG['SEC'])
    d.fillRoundRect(lx, by, 80, K_SZ, 6, shiftBg)
    d.drawRoundRect(lx, by, 80, K_SZ, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['BRIGHT'])); d.drawString("^", lx + 28, by + (K_SZ - 20) // 2)
    lx += 86
    d.fillRoundRect(lx, by, 100, K_SZ, 6, d.color565(0, 60, 30))
    d.drawRoundRect(lx, by, 100, K_SZ, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['BODY']))
    d.drawString("123#" if alpha else "ABC", lx + 12, by + (K_SZ - 20) // 2)
    lx += 106
    remain = 1280 - lx - 20 - 106 - 180
    d.fillRoundRect(lx, by, remain, K_SZ, 6, COL(MG['SEC']))
    d.drawRoundRect(lx, by, remain, K_SZ, 6, COL(MG['LINE']))
    d.setTextColor(COL(MG['BODY'])); d.drawString("Space", lx + remain // 2 - 40, by + (K_SZ - 20) // 2)
    lx += remain + 6
    d.fillRoundRect(lx, by, 100, K_SZ, 6, d.color565(60, MG['SEC'], 0))
    d.drawRoundRect(lx, by, 100, K_SZ, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['DIM'])); d.drawString("DEL", lx + 16, by + (K_SZ - 20) // 2)
    lx += 106
    d.fillRoundRect(lx, by, 180, K_SZ, 6, COL(MG['BTN']))
    d.drawRoundRect(lx, by, 180, K_SZ, 6, COL(MG['BTN_BDR']))
    d.setTextColor(COL(MG['BRIGHT'])); d.drawString("Connect", lx + 14, by + (K_SZ - 20) // 2)
    d.fillRoundRect(20, 660, 120, 44, 6, COL(MG['CANCEL']))
    d.drawRoundRect(20, 660, 120, 44, 6, COL(MG['CANCEL_BDR']))
    d.setTextColor(COL(MG['DIM'])); d.setTextSize(2); d.drawString("Cancel", 36, 672)
    return d

def screen_wifi_connecting(connected=False):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    draw_title_bar(d, "Connecting...", step="3/4")
    d.setTextFont(1); d.setTextSize(2)
    d.setTextColor(COL(MG['DIM']))
    d.drawString('"CMCC-8fZq"', 40, 100)                 # wifi_setup.cpp:331-332
    d.setTextColor(COL(MG['BODY']))
    d.drawString("Connecting...", 40, 160)
    if connected:
        d.setTextColor(COL(MG['BRIGHT'])); d.drawString("Connected!", 40, 210)
        d.setTextColor(COL(MG['DIM']));   d.drawString("touch to continue", 40, 280)
    d.fillRoundRect(40, 590, 160, 40, 6, COL(MG['CANCEL']))
    d.drawRoundRect(40, 590, 160, 40, 6, COL(MG['CANCEL_BDR']))
    d.setTextColor(COL(MG['DIM'])); d.drawString("Cancel", 60, 598)
    return d

def screen_wifi_tz(sel=21):
    d = Screen()
    d.fillScreen(TFT_BLACK)
    draw_title_bar(d, "Select Timezone", step="4/4")
    d.setTextSize(2)                                    # font still Font2 (from title)
    cols, bw, bh, gap = 6, 160, 40, 10
    startX = (1280 - (cols * bw + (cols - 1) * gap)) // 2
    startY = 120
    for i, (label, _off) in enumerate(TZ_LIST):
        bx = startX + (i % cols) * (bw + gap)
        by = startY + (i // cols) * (bh + gap)
        s = (i == sel)
        d.fillRoundRect(bx, by, bw, bh, 4, COL(MG['SEL']) if s else COL(MG['HOVER']))
        d.drawRoundRect(bx, by, bw, bh, 4, COL(MG['BTN_BDR']) if s else COL(MG['LINE']))
        d.setTextColor(COL(MG['BRIGHT']) if s else COL(MG['BODY']))
        lbl = (">" if s else "") + label
        d.drawString(lbl, bx + 4, by + 12)              # wifi_setup.cpp:373
    d.fillRoundRect(540, 560, 200, 48, 6, COL(MG['BTN']))
    d.drawRoundRect(540, 560, 200, 48, 6, COL(MG['BTN_BDR']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['BRIGHT']))
    tw = d.textWidth("Done")
    d.drawString("Done", 540 + (200 - tw) // 2, 560 + (48 - 16) // 2)
    return d

def _time_block(d, synced=True):
    if synced:
        s = "2026-10-04  21:47:03"
        d.setTextFont(2); d.setTextSize(5)
        d.setTextColor(d.color565(MG['ACCENT_R'], MG['ACCENT_G'], MG['ACCENT_B']))
        timeW = d.textWidth("0000-00-00  00:00:00")
        timeY, timeX = 180, (1280 - timeW) // 2
        d.drawString(s, timeX, timeY)
        d.setTextSize(2); d.setTextColor(COL(MG['BODY']))
        d.drawString("Time synced successfully", 440, 320)   # wifi_setup.cpp:435

def screen_wifi_confirm():
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.fillRect(0, 0, 1280, 48, COL(MG['TITLE_BG']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['TITLE']))
    d.drawString("Confirm Current Time", 20, 10)              # wifi_setup.cpp:415
    _time_block(d)
    d.setTextFont(2)
    btnW, btnH, btnGap = 240, 52, 60
    cx, by = 640, 440
    bx1, bx2 = cx - btnW - btnGap // 2, cx + btnGap // 2
    d.fillRoundRect(bx1, by, btnW, btnH, 6, COL(MG['BTN']))
    d.drawRoundRect(bx1, by, btnW, btnH, 6, COL(MG['BTN_BDR']))
    d.setTextSize(2); d.setTextColor(COL(MG['BRIGHT']))
    tw = d.textWidth("Confirm")
    d.drawString("Confirm", bx1 + (btnW - tw) // 2, by + (btnH - 16) // 2)
    d.fillRoundRect(bx2, by, btnW, btnH, 6, COL(MG['SEC']))
    d.drawRoundRect(bx2, by, btnW, btnH, 6, COL(MG['SEC_BDR']))
    d.setTextColor(COL(MG['DIM']))
    tw = d.textWidth("Reselect")
    d.drawString("Reselect", bx2 + (btnW - tw) // 2, by + (btnH - 16) // 2)
    return d

def screen_wifi_ntp_fail():
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.fillRect(0, 0, 1280, 48, COL(MG['TITLE_BG']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['TITLE']))
    d.drawString("Confirm Current Time", 20, 10)
    d.setTextSize(2); d.setTextColor(d.color565(MG['WARN_R'], MG['WARN_G'], 0))
    d.drawString("NTP time sync failed!", (1280 - d.textWidth("NTP time sync failed!")) // 2, 260)
    d.setTextSize(1); d.setTextColor(COL(MG['DIM']))
    d.drawString("Touch below to retry", (1280 - d.textWidth("Touch below to retry")) // 2, 320)
    d.setTextFont(2)
    btnW, btnH = 300, 52
    bx, by = (1280 - btnW) // 2, 440
    d.fillRoundRect(bx, by, btnW, btnH, 6, COL(MG['WARN_BG']))
    d.drawRoundRect(bx, by, btnW, btnH, 6, COL(MG['SEC_BDR']))
    d.setTextSize(2); d.setTextColor(COL(MG['WARN_G']))
    tw = d.textWidth("Retry")
    d.drawString("Retry", bx + (btnW - tw) // 2, by + (btnH - 16) // 2)
    return d

def screen_boot_connect():
    d = Screen()
    d.fillScreen(TFT_BLACK)
    d.fillRect(0, 0, 1280, 48, COL(MG['TITLE_BG']))
    d.setTextFont(2); d.setTextSize(2); d.setTextColor(COL(MG['TITLE']))
    d.drawString("Matrix Rain", 20, 10)                  # wifi_setup.cpp:718-724
    d.setTextSize(2); d.setTextColor(COL(MG['BODY']))
    d.drawString("Connecting to WiFi...", 440, 300)
    d.setTextSize(1); d.setTextColor(COL(MG['DIM']))
    d.drawString("CMCC-8fZq", 480, 340)
    return d

# ═══════════════════════════ annotations ═══════════════════════════
RED = (255, 40, 40)
def mark(d, num, boxes, dot):
    """boxes: list of (x,y,w,h) to outline; dot: (x,y) for the badge."""
    for (x, y, w, h) in boxes:
        d.d.rectangle([x, y, x + w - 1, y + h - 1], outline=RED, width=2)
    bx, by = dot
    bx = max(18, min(1262, bx)); by = max(18, min(702, by))
    d.d.ellipse([bx - 18, by - 18, bx + 18, by + 18], fill=RED, outline=(255, 255, 255), width=2)
    label = str(num)
    old = (d.font, d.size, d.fg)
    d.font, d.size, d.fg = FONT2, 2, (255, 255, 255)
    tw = d.textWidth(label)
    d.drawString(label, bx - tw // 2, by - 16)
    d.font, d.size, d.fg = old

def save(d, name, annotated=True):
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name + ('' if annotated else '_clean') + '.png')
    d.img.save(p)
    return p

# ═══════════════════════════ main ═══════════════════════════
def emit(d, name, marks):
    """Save the clean screen, then the same screen with numbered markers."""
    outs = [save(d, name, annotated=False)]
    for (num, boxes, dot) in marks:
        mark(d, num, boxes, dot)
    outs.append(save(d, name, annotated=True))
    return outs

SCREENS = [
 ('01_setup_menu',   lambda: screen_setup_menu(0, "FULL", 50), [
    (1, [(20, 60, 1240, 56)], (30, 62)),        # row text 4px high of centre
    (2, [(20, 300, 1240, 56)], (30, 356)),      # list stops at y=356, 364px empty
    (3, [(0, 0, 1280, 48)], (700, 24)),         # no title bar / no step context
    (4, [(1100, 120, 70, 56)], (1180, 176)),    # value right edge 1160, 99px dead
 ]),
 ('02_setup_charset', lambda: screen_setup_charset("KATAKANA"), [
    (1, [(0, 0, 1280, 48)], (700, 24)),         # header bar missing on sub-pages
    (2, [(340, 260, 70, 56)], (330, 330)),      # "<" 20/35 off centre
    (3, [(870, 260, 70, 56)], (950, 330)),      # ">" 20/35 off centre
    (4, [(540, 480, 200, 48)], (752, 534)),     # Back label 8px low
 ]),
 ('03_setup_bright', lambda: screen_setup_bright(50), [
    (1, [(0, 0, 1280, 48)], (700, 24)),
    (2, [(340, 330, 80, 56)], (330, 400)),      # "-" 22/43 off centre
    (3, [(860, 330, 80, 56)], (950, 400)),      # "+" 22/43 off centre
    (4, [(430, 410, 420, 40)], (430, 400)),     # debug footnote, off-centre, 1.19:1
    (5, [(540, 500, 200, 48)], (752, 554)),     # Back at y=500 here, y=480 elsewhere
 ]),
 ('04_setup_about', lambda: screen_setup_about(), [
    (1, [(0, 0, 1280, 48)], (700, 24)),
    (2, [(30, 90, 500, 200)], (30, 90)),        # everything hangs off x=40
    (3, [(540, 480, 200, 48)], (752, 534)),
 ]),
 ('05_wifi_scan', lambda: screen_wifi_scan(), [
    (1, [(500, 280, 330, 60)], (500, 280)),     # no progress feedback, 400ms static
 ]),
 ('06_wifi_list', lambda: screen_wifi_list(2), [
    (1, [(40, 680, 160, 36)], (32, 688)),       # "[Scan]" 20/68 off centre + touching bottom
    (2, [(1080, 680, 160, 36)], (1248, 688)),   # Back 40/66 off centre
    (3, [(1080, 676, 160, 40)], (1150, 736)),   # label invisible: 1.09:1 on the fill
 ]),
 ('07_wifi_keyboard', lambda: screen_wifi_keyboard("", False, True), [
    (1, [(18, 82, 1244, 56)], (26, 76)),        # field text inset 12px vs 6px when typed
    (2, [(1086, 494, 180, 108)], (1266, 548)),  # Connect 14/84 off centre
    (3, [(980, 494, 100, 108)], (1080, 602)),   # DEL 16/50 off centre
    (4, [(212, 494, 762, 108)], (974, 602)),    # Space 341/363 off centre
    (5, [(106, 494, 100, 108)], (206, 602)),    # "123#" 14/42 off centre
    (6, [(20, 494, 80, 108)], (100, 602)),      # Shift 28/42 off centre
    (7, [(20, 660, 120, 44)], (140, 672)),      # Cancel 16/36 off centre, dim 1.52:1
 ]),
 ('08_wifi_keyboard_typed', lambda: screen_wifi_keyboard("Sunset#2031", False, True), [
    (1, [(18, 82, 1244, 56)], (26, 76)),        # text jumps 6px left on first keystroke
 ]),
 ('09_wifi_connecting', lambda: screen_wifi_connecting(True), [
    (1, [(0, 0, 1280, 48)], (700, 24)),         # "Connecting..." twice
    (2, [(30, 90, 460, 290)], (30, 90)),        # whole page left-aligned at x=40
    (3, [(40, 590, 160, 40)], (200, 634)),      # Cancel 20/72 off centre
 ]),
 ('10_wifi_tz', lambda: screen_wifi_tz(21), [
    (1, [(135, 120, 160, 40)], (128, 112)),     # labels 4px inset, left-aligned
    (2, [(135, 120, 1010, 200)], (1150, 130)),  # 32px glyphs in a 40px cell: 18/2
    (3, [(540, 560, 200, 48)], (752, 614)),     # Done 8px low
    (4, [(135, 320, 1010, 50)], (640, 385)),    # last row of 4 not centred
 ]),
 ('11_wifi_confirm', lambda: screen_wifi_confirm(), [
    (1, [(470, 320, 500, 40)], (470, 388)),     # "Time synced..." hardcoded x=440
    (2, [(260, 440, 540, 52)], (800, 508)),     # Confirm/Reselect 8px low; Reselect 1.33:1
 ]),
 ('12_wifi_ntp_fail', lambda: screen_wifi_ntp_fail(), [
    (1, [(490, 440, 300, 52)], (800, 508)),     # Retry 8px low (24/2)
 ]),
 ('13_boot_connect', lambda: screen_boot_connect(), [
    (1, [(440, 290, 420, 70)], (440, 290)),     # hardcoded x, not centred
 ]),
]

def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    made = []
    for name, fn, marks in SCREENS:
        if only and not name.startswith(only): continue
        made += emit(fn(), name, marks)
    for p in made:
        print(p)

if __name__ == '__main__':
    main()
