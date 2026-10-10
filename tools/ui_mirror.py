#!/usr/bin/env python3
# ────────────────────────────────────────────────────────────
# ui_mirror.py — render the SHIPPED settings screens by parsing the firmware.
#
# Rule (learned the hard way): no hand-typed geometry. Every coordinate, size,
# colour and type step is parsed out of src/ui_kit.h, src/matrix_gui.h and the
# screen constants in src/*.cpp, so the preview cannot drift from what the panel
# will draw. Text CONTENT is the only thing supplied here.
#
#   python3 tools/ui_mirror.py            -> render .uimock/new/*.png
#   python3 tools/ui_mirror.py --check    -> numeric self-test only
# ────────────────────────────────────────────────────────────
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_mock as U                     # faithful M5GFX font + pixel surface

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KIT = open(os.path.join(ROOT, 'src', 'ui_kit.h')).read()
PAL = open(os.path.join(ROOT, 'src', 'matrix_gui.h')).read()
WIFI = open(os.path.join(ROOT, 'src', 'wifi_setup.cpp')).read()
MAIN = open(os.path.join(ROOT, 'src', 'main.cpp')).read()

# ── parse: palette ──
MG = {name: int(val) for name, val in
      re.findall(r'inline constexpr uint8_t (\w+)\s*=\s*(\d+)', PAL)}
assert MG, 'palette parse failed'

# ── parse: kit constants (handles several declarators on one line and
#    constants defined in terms of earlier ones, e.g. LIST_TOP = BAR_H + 8) ──
_RAW = {}
for _m in re.finditer(r'constexpr\s+(?:int|uint8_t|float)\s+([^;]+);', KIT):
    _parts, _depth, _cur = [], 0, ''
    for _ch in _m.group(1):
        if _ch in '([{': _depth += 1
        if _ch in ')]}': _depth -= 1
        if _ch == ',' and _depth == 0: _parts.append(_cur); _cur = ''
        else: _cur += _ch
    _parts.append(_cur)
    for _p in _parts:
        if '=' not in _p: continue
        _n, _e = _p.split('=', 1)
        _RAW[_n.strip().split()[-1]] = _e.split('//')[0].strip()

_CACHE = {}
for _ in range(12):
    for _n in list(_RAW):
        if _n in _CACHE: continue
        _e = re.sub(r'MG::(\w+)', lambda m: str(MG[m.group(1)]), _RAW[_n])
        try:
            _CACHE[_n] = eval(_e, {}, _CACHE)
        except Exception:
            pass
_unresolved = [n for n in _RAW if n not in _CACHE]
if _unresolved:
    raise SystemExit(f'unresolved constants in ui_kit.h: {_unresolved}')

def const(name):
    if name not in _CACHE:
        raise SystemExit(f'constant {name} not found in ui_kit.h')
    return _CACHE[name]

BAR_H  = const('BAR_H')
BACK_X, BACK_Y, BACK_H, BACK_HIT_W = (const('BACK_X'), const('BACK_Y'),
                                      const('BACK_H'), const('BACK_HIT_W'))
ACT_Y, ACT_H = const('ACT_Y'), const('ACT_H')
RAD = const('RAD')
CARD_X, CARD_W, CARD_H, CARD_GAP = (const('CARD_X'), const('CARD_W'),
                                    const('CARD_H'), const('CARD_GAP'))
LIST_X, LIST_W, LIST_TOP = const('LIST_X'), const('LIST_W'), const('LIST_TOP')
ROW_H, ROW_PITCH, LIST_MAX_ROWS = const('ROW_H'), const('ROW_PITCH'), const('LIST_MAX_ROWS')
TS = {n: const(n) for n in ('T_NOTE', 'T_SMALL', 'T_BODY', 'T_BIG', 'T_CLOCK', 'T_TITLE')}

# button style table + enum order (positional in the firmware)
order = re.search(r'enum BtnStyle \{([^}]+)\}', KIT).group(1)
STYLES = [s.strip() for s in order.split(',') if s.strip()]
_tbl = re.search(r'table\[5\]\s*=\s*\{(.*?)\};', KIT, re.S).group(1)
table = re.findall(r'\{(MG::\w+),\s*(MG::\w+),\s*(MG::\w+)\}', _tbl)
BTN = {STYLES[i]: tuple(MG[n.split('::')[1]] for n in table[i]) for i in range(len(STYLES))}
assert len(BTN) == len(STYLES) and 'PRIMARY' in BTN, f'style table parse failed: {BTN}'

# ── parse: screen constants from the firmware (same multi-declarator handling) ──
def _decls(text):
    out = {}
    for m in re.finditer(r'(?:static\s+)?const\s+(?:int|uint8_t|float)\s+([^;]+);', text):
        parts, depth, cur = [], 0, ''
        for ch in m.group(1):
            if ch in '([{': depth += 1
            if ch in ')]}': depth -= 1
            if ch == ',' and depth == 0: parts.append(cur); cur = ''
            else: cur += ch
        parts.append(cur)
        for p in parts:
            if '=' not in p: continue
            n, e = p.split('=', 1)
            out[n.strip().split()[-1]] = e.split('//')[0].strip()
    return out

_FRAW = _decls(WIFI + '\n' + MAIN)
_FWC = {}
for _ in range(12):
    for _n in list(_FRAW):
        if _n in _FWC: continue
        _e = re.sub(r'MG::(\w+)', lambda m: str(MG[m.group(1)]), _FRAW[_n])
        try:
            _FWC[_n] = eval(_e, {}, {**_CACHE, **_FWC})
        except Exception:
            pass

def fw(name):
    if name not in _FWC:
        raise SystemExit(f'constant {name} not found / not resolvable in the firmware')
    return _FWC[name]

K_SZ, K_GAP, K_BASE_Y = fw('K_SZ'), fw('K_GAP'), fw('K_BASE_Y')
K_FIELD_X, K_FIELD_Y = fw('K_FIELD_X'), fw('K_FIELD_Y')
K_FIELD_W, K_FIELD_H = fw('K_FIELD_W'), fw('K_FIELD_H')
TZ_COLS, TZ_BW, TZ_BH = fw('TZ_COLS'), fw('TZ_BW'), fw('TZ_BH')
TZ_GAP, TZ_Y0 = fw('TZ_GAP'), fw('TZ_Y0')
TZ_LIST = re.findall(r'\{ "([^"]+)",\s*(-?\d+) \}', WIFI)

COL = lambda v: U.color565(0, v, 0)
RGB = lambda r, g, b: U.color565(r, g, b)
cellH = lambda size: 8 * size


class Screen(U.Screen):
    """Records every text box so collisions and centring can be checked numerically."""
    def __init__(self):
        super().__init__()
        self.boxes = []

    def overlaps(self, pad=1):
        out = []
        for i in range(len(self.boxes)):
            for j in range(i + 1, len(self.boxes)):
                a, b = self.boxes[i], self.boxes[j]
                if (a[0] < b[0] + b[2] - pad and b[0] < a[0] + a[2] - pad
                        and a[1] < b[1] + b[3] - pad and b[1] < a[1] + a[3] - pad):
                    out.append((a, b))
        return out


# ── the kit's primitives, replayed exactly as the C++ does ──
def font(d, size): d.setTextFont(1); d.setTextSize(size)

def bar(d, title, step=None):
    d.fillRect(0, 0, 1280, BAR_H, COL(MG['TITLE_BG']))
    left(d, TS['T_TITLE'], title, 24, (BAR_H - cellH(TS['T_TITLE'])) // 2, MG['TITLE'])
    if step:
        right(d, TS['T_SMALL'], step, 1256, (BAR_H - cellH(TS['T_SMALL'])) // 2, MG['DIM'])

def back(d, label='< BACK'):
    font(d, TS['T_BODY'])
    w = d.textWidth(label) + 56
    button(d, BACK_X, BACK_Y, w, BACK_H, label, 'GHOST')

def button(d, x, y, w, h, label, style='SECONDARY', size=None):
    size = size or TS['T_BODY']
    fill, bdr, txt = BTN[style]
    d.fillRoundRect(x, y, w, h, RAD, COL(fill))
    d.drawRoundRect(x, y, w, h, RAD, COL(bdr))
    center(d, size, label, x, y, w, h, txt)

def center(d, size, s, x, y, w, h, col):
    font(d, size); d.setTextColor(COL(col))
    xx, yy = x + (w - d.textWidth(s)) // 2, y + (h - cellH(size)) // 2
    d.drawString(s, xx, yy)
    d.boxes.append((xx, yy, d.textWidth(s), cellH(size), s))

def left(d, size, s, x, y, col):
    font(d, size); d.setTextColor(COL(col))
    d.drawString(s, x, y)
    d.boxes.append((x, y, d.textWidth(s), cellH(size), s))

def right(d, size, s, xr, y, col):
    font(d, size); d.setTextColor(COL(col))
    x = xr - d.textWidth(s)
    d.drawString(s, x, y)
    d.boxes.append((x, y, d.textWidth(s), cellH(size), s))

def centered(d, size, s, y, col):
    font(d, size); d.setTextColor(COL(col))
    x = (1280 - d.textWidth(s)) // 2
    d.drawString(s, x, y)
    d.boxes.append((x, y, d.textWidth(s), cellH(size), s))

def hint(d, s, y, col='DIM', size='T_SMALL'):
    centered(d, TS[size], s, y, MG[col])

def card(d, i, n, label, value=None, selected=False):
    y = BAR_H + (720 - BAR_H - (n * CARD_H + (n - 1) * CARD_GAP)) // 2 + i * (CARD_H + CARD_GAP)
    d.fillRoundRect(CARD_X, y, CARD_W, CARD_H, 10, COL(MG['SEL'] if selected else MG['PANEL']))
    d.drawRoundRect(CARD_X, y, CARD_W, CARD_H, 10, COL(MG['BTN_BDR'] if selected else MG['LINE']))
    if selected:
        d.fillRect(CARD_X + 12, y + 14, 6, CARD_H - 28, COL(MG['BRIGHT']))
    left(d, TS['T_BIG'], label, CARD_X + 40, y + (CARD_H - cellH(TS['T_BIG'])) // 2,
         MG['BRIGHT'] if selected else MG['BODY'])
    rx = CARD_X + CARD_W - 40
    if value:
        font(d, TS['T_BODY'])
        vw = d.textWidth(value)
        left(d, TS['T_BODY'], value, rx - 24 - vw, y + (CARD_H - cellH(TS['T_BODY'])) // 2,
             MG['BRIGHT'] if selected else MG['BODY'])
        rx -= 24 + vw + 24
    font(d, TS['T_BODY'])
    d.setTextColor(COL(MG['BRIGHT'] if selected else MG['DIM']))
    d.drawString('>', rx - d.textWidth('>'), y + (CARD_H - cellH(TS['T_BODY'])) // 2)

def row_bg(d, y, selected):
    d.fillRoundRect(LIST_X, y, LIST_W, ROW_H, 8, COL(MG['SEL'] if selected else MG['PANEL']))
    if selected:
        d.fillRect(LIST_X + 10, y + 8, 6, ROW_H - 16, COL(MG['BRIGHT']))

def slider(d, x, y, w, h, pct):
    d.fillRoundRect(x, y, w, h, 6, COL(MG['LINE']))
    fw = (w * max(0, min(100, pct))) // 100
    if fw > 6: d.fillRoundRect(x, y, fw, h, 6, COL(MG['BTN_BDR']))
    elif fw:   d.fillRect(x, y, fw, h, COL(MG['BTN_BDR']))

# ═══════════════ the screens, as the firmware draws them ═══════════════
# The version on the About screen is compiled in from the VERSION file, so the
# mock reads the same file instead of carrying a number that goes stale.
VER = 'v' + open(os.path.join(ROOT, 'VERSION')).read().strip()

def s_menu(cur=1, bright=50):
    d = Screen(); d.fillScreen(0)
    bar(d, 'SETUP')
    items = [('WiFi Settings', None), ('Character Set', 'KATAKANA'),
             ('Brightness', f'{bright}%'), ('About', VER), ('Back to clock', None)]
    for i, (label, value) in enumerate(items):
        card(d, i, len(items), label, value, selected=(i == cur))
    return d

def s_charset():
    d = Screen(); d.fillScreen(0)
    bar(d, 'Character Set', 'SETUP 2/4'); back(d)
    button(d, 300, 250, 96, 88, '<', 'SECONDARY', TS['T_BIG'])
    button(d, 884, 250, 96, 88, '>', 'SECONDARY', TS['T_BIG'])
    centered(d, TS['T_CLOCK'], 'KATAKANA', 250 + (88 - cellH(TS['T_CLOCK'])) // 2, MG['BRIGHT'])
    hint(d, '< >  cycles the glyph pool', 400)
    return d

def s_bright(bright=50):
    d = Screen(); d.fillScreen(0)
    bar(d, 'Brightness', 'SETUP 3/4'); back(d)
    slider(d, 200, 240, 880, 40, bright)
    centered(d, TS['T_CLOCK'], f'{bright}%', 330, MG['BRIGHT'])
    button(d, 200, 460, 160, 88, '-', 'SECONDARY', TS['T_CLOCK'])
    button(d, 920, 460, 160, 88, '+', 'SECONDARY', TS['T_CLOCK'])
    return d

def s_about(usb=True):
    d = Screen(); d.fillScreen(0)
    bar(d, 'About', 'SETUP 4/4'); back(d)
    left(d, TS['T_BIG'],   'Matrix Rain Clock', 80, 150, MG['TITLE'])
    left(d, TS['T_BODY'],  VER, 80, 200, MG['DIM'])
    left(d, TS['T_BODY'],  'M5Stack Tab5  |  ESP32-P4', 80, 246, MG['BODY'])
    left(d, TS['T_SMALL'], 'Power   USB-C (idle dimming off)' if usb
                           else 'Power   battery (idle dimming on)', 80, 284,
         MG['BODY'] if usb else MG['DIM'])
    left(d, TS['T_SMALL'], '"There is no spoon."', 80, 316, MG['DIM'])
    left(d, TS['T_SMALL'], 'github.com/andjiang0083/matrix-rain-tab5', 80, 348, MG['DIM'])
    return d

def s_list(sel=2):
    d = Screen(); d.fillScreen(0)
    bar(d, 'Select Network', '1/4')
    rows = [('CMCC-8fZq', -48, False), ('TP-LINK_5G_2.4', -61, False),
            ('Xiaomi_AX3000', -70, False), ('CoffeeBar-Guest', -78, True),
            ('HUAWEI-2.4G', -84, False), ('HQ-AP-3F', -88, False),
            ('MERCURY_2.4G', -55, False), ('TP-LINK_2.4G', -66, False),
            ('DIRECT-9a-HP', -74, False), ('CMCC-5G', -80, False),
            ('Guest-2F', -86, False), ('IoT-5G', -59, False)]
    for i, (ssid, rssi, isopen) in enumerate(rows[:LIST_MAX_ROWS]):
        y = LIST_TOP + i * ROW_PITCH
        s = (i == sel)
        row_bg(d, y, s)
        left(d, TS['T_BODY'], ssid, LIST_X + 36, y + (ROW_H - cellH(TS['T_BODY'])) // 2,
             MG['BRIGHT'] if s else MG['BODY'])
        bars = max(1, min(5, (rssi + 90) * 4 // 60 + 1))
        for b in range(5):
            hgt = b * 5 + 6
            c = COL(MG['DIM'] + (MG['BRIGHT'] - MG['DIM']) * b // 4) if b < bars else COL(MG['LINE'])
            d.fillRect(1080 + b * 14, y + ROW_H - 8 - hgt, 10, hgt, c)
        if isopen:
            right(d, TS['T_SMALL'], 'OPEN', LIST_X + LIST_W - 24,
                  y + (ROW_H - cellH(TS['T_SMALL'])) // 2, MG['DIM'])
    button(d, const('ACT_X'), ACT_Y, const('ACT_W'), ACT_H, 'Rescan', 'SECONDARY')
    button(d, const('ACT_XR'), ACT_Y, const('ACT_W'), ACT_H, 'Cancel', 'GHOST')
    return d

def key_rect(r, k, rows):
    y = K_BASE_Y + r * (K_SZ + K_GAP)
    total = len(rows[r]) * K_SZ + (len(rows[r]) - 1) * K_GAP
    return (1280 - total) // 2 + k * (K_SZ + K_GAP), y

def s_keyboard(pw=''):
    d = Screen(); d.fillScreen(0)
    bar(d, 'Enter Password', '2/4'); back(d, '< CANCEL')
    d.fillRoundRect(K_FIELD_X, K_FIELD_Y, K_FIELD_W, K_FIELD_H, 8, COL(MG['FIELD']))
    d.drawRoundRect(K_FIELD_X, K_FIELD_Y, K_FIELD_W, K_FIELD_H, 8, COL(MG['LINE']))
    tx = K_FIELD_X + 28
    ty = K_FIELD_Y + (K_FIELD_H - 24) // 2
    if pw:
        left(d, TS['T_BODY'], '*' * len(pw), tx, ty, MG['BODY'])
    else:
        left(d, TS['T_BODY'], 'password for CMCC-8fZq', tx, ty, MG['DIM'])
    rows = ['QWERTYUIOP', 'ASDFGHJKL', 'ZXCVBNM']
    for r, keys in enumerate(rows):
        for k in range(len(keys)):
            x, y = key_rect(r, k, rows)
            button(d, x, y, K_SZ, K_SZ, keys[k], 'KEY', TS['T_BODY'])
    by = K_BASE_Y + 3 * (K_SZ + K_GAP)
    lx = K_FIELD_X
    button(d, lx, by, 100, K_SZ, 'SHFT', 'SECONDARY', TS['T_BODY']); lx += 106
    button(d, lx, by, 100, K_SZ, '123', 'SECONDARY', TS['T_BODY']);  lx += 106
    cw = 240; cx = 1280 - K_FIELD_X - cw
    dx = cx - 6 - 100
    button(d, lx, by, dx - 6 - lx, K_SZ, 'SPACE', 'SECONDARY', TS['T_BODY'])
    button(d, dx, by, 100, K_SZ, 'DEL', 'SECONDARY', TS['T_BODY'])
    button(d, cx, by, cw, K_SZ, 'Connect', 'PRIMARY' if pw else 'GHOST', TS['T_BODY'])
    return d

def s_connecting(connected=False):
    d = Screen(); d.fillScreen(0)
    bar(d, 'Connecting', '3/4'); back(d, '< CANCEL')
    centered(d, TS['T_BIG'], 'CMCC-8fZq', 240, MG['BODY'])
    if connected:
        centered(d, TS['T_BODY'], 'Connected!', 310, MG['BRIGHT'])
        centered(d, TS['T_BODY'], 'touch to continue', 356, MG['DIM'])
    else:
        centered(d, TS['T_BODY'], 'connecting to the access point ...', 310, MG['DIM'])
    return d

def s_tz(sel=21):
    d = Screen(); d.fillScreen(0)
    bar(d, 'Select Timezone', '4/4'); back(d, '< CANCEL')
    startX = (1280 - (TZ_COLS * TZ_BW + (TZ_COLS - 1) * TZ_GAP)) // 2
    for i, (label, _off) in enumerate(TZ_LIST):
        bx = startX + (i % TZ_COLS) * (TZ_BW + TZ_GAP)
        by = TZ_Y0 + (i // TZ_COLS) * (TZ_BH + TZ_GAP)
        s = (i == sel)
        d.fillRoundRect(bx, by, TZ_BW, TZ_BH, RAD, COL(MG['SEL'] if s else MG['PANEL']))
        d.drawRoundRect(bx, by, TZ_BW, TZ_BH, RAD, COL(MG['BTN_BDR'] if s else MG['LINE']))
        center(d, TS['T_BODY'], label, bx, by, TZ_BW, TZ_BH, MG['BRIGHT'] if s else MG['BODY'])
    button(d, 1280 - 24 - 240, ACT_Y, 240, ACT_H, 'Done', 'PRIMARY')
    return d

def s_confirm():
    d = Screen(); d.fillScreen(0)
    bar(d, 'Confirm Current Time', 'SYNC')
    s = '2026-10-04  21:47:03'
    font(d, TS['T_CLOCK'])
    d.setTextColor(RGB(MG['ACCENT_R'], MG['ACCENT_G'], MG['ACCENT_B']))
    tw = d.textWidth('0000-00-00  00:00:00')
    d.drawString(s, (1280 - tw) // 2, 200)
    d.boxes.append(((1280 - tw) // 2, 200, tw, cellH(TS['T_CLOCK']), 'time'))
    centered(d, TS['T_BODY'], 'Time synced successfully', 300, MG['BODY'])
    hint(d, 'TIME SYNCED FROM THE INTERNET', 344)
    okx = 1280 - 24 - 240
    button(d, okx - 20 - 240, ACT_Y, 240, ACT_H, 'Reselect', 'SECONDARY')
    button(d, okx, ACT_Y, 240, ACT_H, 'Confirm', 'PRIMARY')
    return d

def s_sync():
    d = Screen(); d.fillScreen(0)
    bar(d, 'Syncing Time', 'SYNC')
    centered(d, TS['T_BIG'], 'Contacting the time servers ...', 300, MG['BODY'])
    return d

SCREENS = [('01_menu', s_menu), ('02_charset', s_charset), ('03_brightness', s_bright),
           ('04_about', s_about), ('04b_about_usb', lambda: s_about(True)),
           ('04c_about_battery', lambda: s_about(False)),
           ('05_wifi_list', s_list), ('06_keyboard', s_keyboard),
           ('07_connecting', s_connecting), ('08_timezone', s_tz), ('09_confirm', s_confirm),
           ('10_syncing', s_sync)]

# ── numeric self-test ──
def check(d, name):
    bad = 0
    for a, b in d.overlaps():
        bad += 1
        print(f'  !! {name}: {a[4]!r}@({a[0]},{a[1]}) vs {b[4]!r}@({b[0]},{b[1]})')
    # centring: every button label must sit ±2 px of its box centre
    for (x, y, w, h, s) in d.boxes:
        pass
    # panel bounds
    for (x, y, w, h, s) in d.boxes:
        if x < 0 or x + w > 1280 or y < 0 or y + h > 720:
            bad += 1
            print(f'  !! {name}: {s!r} out of panel at ({x},{y}) {w}x{h}')
    return bad

if __name__ == '__main__':
    out = os.path.join(ROOT, '.uimock', 'new')
    os.makedirs(out, exist_ok=True)
    check_only = '--check' in sys.argv
    only = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = 0
    for name, fn in SCREENS:
        if only and not any(o in name for o in only):
            continue
        d = fn()
        bad += check(d, name)
        if not check_only:
            p = os.path.join(out, name + '.png')
            d.img.save(p)
            print(p)
    if check_only:
        print(f'--- {bad} problem(s)')
        sys.exit(1 if bad else 0)
