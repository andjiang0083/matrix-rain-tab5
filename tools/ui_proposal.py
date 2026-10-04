#!/usr/bin/env python3
# ────────────────────────────────────────────────────────────
# ui_proposal.py — design preview for the SETTINGS-UI unification pass.
# NOT a firmware mirror: it renders the *proposed* spec so the look can be
# approved before any C++ changes. Uses ui_mock's faithful Screen/font stack
# (M5GFX Font0 = GLCD 5x7, integer-scaled) so what you see is what the panel
# can actually draw.
#
# Spec under discussion:
#   one font family  : GLCD 5x7 (Font0), integer scale only -> mosaic look
#   type scale       : T1 32px / T2 24px / T3 16px / T4 40px (clock)
#   title bar        : 56px, back control top-left, step id right
#   action row       : y=632 h=56  (primary right, secondary left)
#   buttons          : one helper, label centered from the REAL glyph metrics
#                      primary / secondary / ghost / warn
# ────────────────────────────────────────────────────────────
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_mock as U

MG = U.MG
COL = U.COL
BLACK = 0

BAR_H   = 56
ACT_Y, ACT_H = 632, 56
RAD     = 8

class Screen(U.Screen):
    """Screen that records every drawString box, so label collisions can be
    detected numerically instead of by eye (the type-scale sheet shipped with
    two labels printed on top of each other once — this is that guard)."""
    def __init__(self):
        super().__init__()
        self.boxes = []
    def cellH(self):
        return (U.FONT0.height if self.font is U.FONT0 else U.FONT2.height) * self.size
    def drawString(self, s, x, y):
        super().drawString(s, x, y)
        self.boxes.append((x, y, self.textWidth(s), self.cellH(), s))
    def overlaps(self, pad=1):
        out = []
        for i in range(len(self.boxes)):
            for j in range(i + 1, len(self.boxes)):
                a, b = self.boxes[i], self.boxes[j]
                if (a[0] < b[0] + b[2] - pad and b[0] < a[0] + a[2] - pad
                        and a[1] < b[1] + b[3] - pad and b[1] < a[1] + a[3] - pad):
                    out.append((a, b))
        return out

def font(d, size):  d.setTextFont(1); d.setTextSize(size)   # 1 -> Font0 (GLCD)
def cell_h(size):   return 8 * size

def txt(d, size, s, x, y, v, bg=BLACK):
    font(d, size); d.setTextColor(COL(v) if bg == BLACK else COL(v)); d.drawString(s, x, y)

def mid(d, size, s, x, y, w, h, v):
    """draw s centered in the box (x,y,w,h) using the real ink cell height"""
    font(d, size)
    d.setTextColor(COL(v))
    d.drawString(s, x + (w - d.textWidth(s)) // 2, y + (h - cell_h(size)) // 2)

def btn(d, x, y, w, h, label, style='secondary', size=3):
    fills = {'primary':   (MG['BTN'],    MG['BTN_BDR'], MG['BRIGHT']),
             'secondary': (MG['SEC'],    MG['SEC_BDR'], MG['BODY']),
             'ghost':     (MG['CANCEL'], MG['CANCEL_BDR'], MG['BODY']),
             'warn':      (MG['WARN_BG'], MG['SEC_BDR'], MG['WARN_G'])}
    f, b, t = fills[style]
    d.fillRoundRect(x, y, w, h, RAD, COL(f))
    d.drawRoundRect(x, y, w, h, RAD, COL(b))
    mid(d, size, label, x, y, w, h, t)

def bar(d, title, step=None, back=None):
    d.fillRect(0, 0, 1280, BAR_H, COL(MG['TITLE_BG']))
    x = 24
    if back:
        font(d, 3)
        s = '< ' + back
        tw = d.textWidth(s)
        w = tw + 36
        d.fillRoundRect(x, (BAR_H - 40) // 2, w, 40, RAD, COL(MG['CANCEL']))
        d.drawRoundRect(x, (BAR_H - 40) // 2, w, 40, RAD, COL(MG['CANCEL_BDR']))
        mid(d, 3, s, x, (BAR_H - 40) // 2, w, 40, MG['BODY'])
        x += w + 26
    txt(d, 3, title, x, (BAR_H - 24) // 2, MG['TITLE'])
    if step:
        font(d, 3)
        txt(d, 3, step, 1256 - d.textWidth(step), (BAR_H - 24) // 2, MG['DIM'])
    return x

def card(d, x, y, w, h, label, value=None, selected=False):
    d.fillRoundRect(x, y, w, h, 10, COL(MG['SEL'] if selected else MG['PANEL']))
    d.drawRoundRect(x, y, w, h, 10, COL(MG['BTN_BDR'] if selected else MG['LINE']))
    if selected:
        d.fillRect(x + 12, y + 14, 6, h - 28, COL(MG['BRIGHT']))
    txt(d, 4, label, x + 40, y + (h - 32) // 2, MG['BRIGHT'] if selected else MG['BODY'])
    right = x + w - 40
    if value:
        font(d, 3)
        tw = d.textWidth(value)
        txt(d, 3, value, right - 24 - tw, y + (h - 24) // 2, MG['BODY'])
        right -= 24 + tw + 24
    txt(d, 3, '>', right - 12, y + (h - 24) // 2, MG['BODY'] if selected else MG['DIM'])

# ═══════════════ samples ═══════════════
def s_font_spec():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'TYPE SCALE', 'SPEC')
    C_NAME, C_OLD, C_ARROW, C_NEW, C_TAG = 40, 250, 740, 800, 1256
    # header
    txt(d, 2, 'CURRENT (firmware mix)', C_OLD, 74, MG['DIM'])
    txt(d, 2, 'PROPOSED (one family, integer scale)', C_NEW, 74, MG['DIM'])
    d.fillRect(40, 96, 1216, 1, COL(MG['LINE']))
    rows = [('page title',     (2, 2), 'Brightness',      4),
            ('button / list',  (1, 2), 'Character Set',   3),
            ('secondary info', (1, 2), 'CMCC-8fZq  OPEN', 2),
            ('big number',     (2, 3), '50%',             4),
            ('clock',          (2, 5), '21:47',           5)]
    y = 116
    for name, (of, osz), s, nsz in rows:
        d.setTextFont(of); d.setTextSize(osz)
        oldH, oldW = d.cellH(), d.textWidth(s)
        font(d, nsz)
        newH, newW = d.cellH(), d.textWidth(s)
        band = max(oldH, newH)
        txt(d, 2, name, C_NAME, y + (band - 16) // 2, MG['DIM'])
        d.setTextFont(of); d.setTextSize(osz); d.setTextColor(COL(MG['BODY']))
        d.drawString(s, C_OLD, y + (band - oldH) // 2)
        font(d, 2); d.setTextColor(COL(MG['LINE']))
        d.drawString('->', C_ARROW, y + (band - 16) // 2)
        font(d, nsz); d.setTextColor(COL(MG['BRIGHT']))
        d.drawString(s, C_NEW, y + (band - newH) // 2)
        tag = f'{nsz*8}px'
        font(d, 2); d.setTextColor(COL(MG['DIM']))
        d.drawString(tag, C_TAG - d.textWidth(tag), y + (band - 16) // 2)
        d.fillRect(40, y + band + 20, 1216, 1, COL(MG['LINE']))
        y += band + 40
    mid(d, 2, 'same 5x7 glyphs as the rain, scaled 2x / 3x / 4x / 5x - blockier, no mixed families',
        0, y + 6, 1280, 24, MG['DIM'])
    return d

def s_charset(back_in_bar=True):
    d = Screen()
    d.fillScreen(BLACK)
    x = bar(d, 'Character Set', 'SETUP 2/3', back='BACK') if back_in_bar \
        else bar(d, 'Character Set', 'SETUP 2/3')
    if not back_in_bar:
        btn(d, 24, 84, 168, 44, '< BACK', 'ghost')
    cy = 250
    btn(d, 300, cy, 96, 88, '<', 'secondary', 4)
    btn(d, 884, cy, 96, 88, '>', 'secondary', 4)
    font(d, 5)
    d.setTextColor(COL(MG['BRIGHT']))
    d.drawString('KATAKANA', (1280 - d.textWidth('KATAKANA')) // 2, cy + (88 - 40) // 2)
    mid(d, 2, '< >  cycles the glyph pool', 0, 400, 1280, 28, MG['DIM'])
    return d

def s_menu():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'SETUP')
    items = [('WiFi Settings', None), ('Character Set', 'KATAKANA'),
             ('Brightness', '50%'), ('About', 'v1.3.2')]
    H, GAP = 88, 10
    top = BAR_H + (720 - BAR_H - (len(items) + 1) * H - len(items) * GAP) // 2
    y = top
    for i, (label, value) in enumerate(items):
        card(d, 80, y, 1120, H, label, value, selected=(i == 1))
        y += H + GAP
    card(d, 80, y, 1120, H, 'Back to clock', None)
    return d

def s_bright():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Brightness', 'SETUP 3/3', back='BACK')
    txt(d, 2, 'PANEL BACKLIGHT  ·  GPIO22 LEDC 12-BIT', 80, 96, MG['DIM'])
    barX, barY, barW, barH = 200, 240, 880, 40
    d.fillRoundRect(barX, barY, barW, barH, 6, COL(MG['LINE']))
    d.fillRoundRect(barX, barY, barW * 50 // 100, barH, 6, COL(MG['BTN_BDR']))
    font(d, 5)
    d.setTextColor(COL(MG['BRIGHT']))
    d.drawString('50%', (1280 - d.textWidth('50%')) // 2, 330)
    btn(d, 200, 460, 160, 88, '-', 'secondary', 4)
    btn(d, 920, 460, 160, 88, '+', 'secondary', 4)
    btn(d, 1020, ACT_Y, 240, ACT_H, 'Save', 'primary')
    btn(d, 760, ACT_Y, 240, ACT_H, 'Reset', 'ghost')
    return d

def s_list():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Select Network', '1/4', back='CANCEL')
    rows = [('CMCC-8fZq', -48, False), ('TP-LINK_5G_2.4', -61, False),
            ('Xiaomi_AX3000', -70, False), ('CoffeeBar-Guest', -78, True),
            ('HUAWEI-2.4G', -84, False), ('HQ-AP-3F', -88, False),
            ('MERCURY_2.4G', -55, False), ('TP-LINK_2.4G', -66, False),
            ('DIRECT-9a-HP', -74, False), ('CMCC-5G', -80, False)]
    H, GAP = 48, 4
    y = BAR_H + 8
    for i, (ssid, rssi, isopen) in enumerate(rows):
        sel = (i == 2)
        d.fillRect(24, y, 1232, H, COL(MG['SEL'] if sel else MG['PANEL']))
        if sel:
            d.fillRect(24, y, 6, H, COL(MG['BRIGHT']))
        txt(d, 3, ssid, 52, y + (H - 24) // 2, MG['BRIGHT'] if sel else MG['BODY'])
        bars = max(1, min(5, (rssi + 90) * 4 // 60 + 1))
        for b in range(5):
            on = b < bars
            v = MG['DIM'] + (MG['BRIGHT'] - MG['DIM']) * b // 4 if on else MG['LINE']
            d.fillRect(1080 + b * 14, y + H - 8 - (b * 5 + 6), 10, b * 5 + 6, COL(v))
        if isopen:
            txt(d, 2, 'OPEN', 1160, y + (H - 16) // 2, MG['DIM'])
        y += H + GAP
    btn(d, 24, ACT_Y, 240, ACT_H, 'Rescan', 'secondary')
    return d

def s_keyboard(pw=''):
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Enter Password', '2/4', back='CANCEL')
    txt(d, 3, 'CMCC-8fZq', 40, 76, MG['DIM'])
    d.fillRoundRect(40, 112, 1200, 64, 8, COL(MG['FIELD']))
    d.drawRoundRect(40, 112, 1200, 64, 8, COL(MG['LINE']))
    txt(d, 3, pw if pw else 'tap the keys below', 68, 112 + (64 - 24) // 2,
        MG['BODY'] if pw else MG['DIM'])
    KS, KG, KBY = 108, 6, 196
    rows = ['QWERTYUIOP', 'ASDFGHJKL', 'ZXCVBNM']
    for r, keys in enumerate(rows):
        yy = KBY + r * (KS + KG)
        xx = (1280 - (len(keys) * KS + (len(keys) - 1) * KG)) // 2
        for k in keys:
            d.fillRoundRect(xx, yy, KS, KS, RAD, COL(MG['HOVER']))
            d.drawRoundRect(xx, yy, KS, KS, RAD, COL(MG['LINE']))
            mid(d, 3, k, xx, yy, KS, KS, MG['BODY'])
            xx += KS + KG
    by = KBY + 3 * (KS + KG)
    lx = 40
    btn(d, lx, by, 100, KS, 'SHFT', 'secondary', 2); lx += 106
    btn(d, lx, by, 100, KS, '123', 'secondary', 2); lx += 106
    sp = 1280 - 40 - lx - 106 - 240
    btn(d, lx, by, sp, KS, 'SPACE', 'secondary', 2); lx += sp + 6
    btn(d, lx, by, 100, KS, 'DEL', 'secondary', 2); lx += 106
    btn(d, lx, by, 240, KS, 'Connect', 'primary' if pw else 'ghost', 3)
    if not pw:
        txt(d, 2, 'type at least one character to connect', 40, 664, MG['DIM'])
    return d

def s_tz():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Select Timezone', '4/4', back='CANCEL')
    TZ = [t[0] for t in U.TZ_LIST]
    cols, bw, bh, gap = 6, 190, 48, 10
    startX = (1280 - (cols * bw + (cols - 1) * gap)) // 2
    startY = 90
    for i, label in enumerate(TZ):
        bx = startX + (i % cols) * (bw + gap)
        by = startY + (i // cols) * (bh + gap)
        sel = (i == 21)
        d.fillRoundRect(bx, by, bw, bh, RAD, COL(MG['SEL'] if sel else MG['PANEL']))
        d.drawRoundRect(bx, by, bw, bh, RAD, COL(MG['BTN_BDR'] if sel else MG['LINE']))
        mid(d, 3, label, bx, by, bw, bh, MG['BRIGHT'] if sel else MG['BODY'])
    btn(d, 24, ACT_Y, 240, ACT_H, 'Rescan', 'ghost')
    btn(d, 1020, ACT_Y, 240, ACT_H, 'Done', 'primary')
    return d

def s_confirm():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Confirm Current Time', 'SYNC')
    font(d, 5)
    d.setTextColor(d.color565(MG['ACCENT_R'], MG['ACCENT_G'], MG['ACCENT_B']))
    s = '2026-10-04  21:47:03'
    d.drawString(s, (1280 - d.textWidth(s)) // 2, 200)
    mid(d, 3, 'TIME SYNCED FROM POOL.NTP.ORG', 0, 280, 1280, 30, MG['BODY'])
    btn(d, 760, ACT_Y, 240, ACT_H, 'Reselect', 'secondary')
    btn(d, 1020, ACT_Y, 240, ACT_H, 'Confirm', 'primary')
    return d

def s_connecting():
    d = Screen()
    d.fillScreen(BLACK)
    bar(d, 'Connecting', '3/4', back='CANCEL')
    mid(d, 4, 'CMCC-8fZq', 0, 240, 1280, 40, MG['BODY'])
    mid(d, 3, 'connecting to the access point ...', 0, 310, 1280, 30, MG['DIM'])
    return d

SCREENS = [('10_type_scale', s_font_spec),
           ('11_back_in_bar', lambda: s_charset(True)),
           ('12_back_below_bar', lambda: s_charset(False)),
           ('13_menu', s_menu),
           ('14_brightness', s_bright),
           ('15_wifi_list', s_list),
           ('16_keyboard', lambda: s_keyboard('')),
           ('17_timezone', s_tz),
           ('18_confirm', s_confirm),
           ('19_connecting', s_connecting)]

if __name__ == '__main__':
    out = os.environ.get('UI_MOCK_OUT', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '.uimock', 'prop'))
    os.makedirs(out, exist_ok=True)
    args  = [a for a in sys.argv[1:] if not a.startswith('--')]
    only  = args[0] if args else None
    check = '--check' in sys.argv
    bad = 0
    for name, fn in SCREENS:
        if only and only not in name: continue
        d = fn()
        if check:
            ov = d.overlaps()
            if ov:
                bad += 1
                print(f'!! {name}: {len(ov)} overlapping label pair(s)')
                for a, b in ov[:8]:
                    print(f'     {a[4]!r} @({a[0]},{a[1]}) {a[2]}x{a[3]}  vs  {b[4]!r} @({b[0]},{b[1]}) {b[2]}x{b[3]}')
            else:
                print(f'ok {name}')
            continue
        p = os.path.join(out, name + '.png')
        d.img.save(p)
        print(p)
    if check:
        print(f'--- {bad} screen(s) with overlapping labels')
        sys.exit(1 if bad else 0)
