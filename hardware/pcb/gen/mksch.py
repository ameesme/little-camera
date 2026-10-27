# -*- coding: utf-8 -*-
"""Generate the full little-camera schematic from design.py.

Style: every pin gets a short wire stub terminated in a global label carrying
the net name. No implicit connectivity, nothing depends on wire geometry
lining up, and the result can be diffed straight back against design.CONN.
"""
import re, uuid, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS, CONN
CONN = {r: {str(k): v for k, v in d.items()} for r, d in CONN.items()}

SCH = sys.argv[1]
src = open(SCH).read()

# ---------------------------------------------------------------- parse ----
def find_block(s, start):
    """Return (block_text, end_index) for the s-expression starting at `start`."""
    d = 0; i = start
    while True:
        if s[i] == '"':
            i += 1
            while s[i] != '"' or s[i-1] == '\\': i += 1
        elif s[i] == '(': d += 1
        elif s[i] == ')':
            d -= 1
            if d == 0: return s[start:i+1], i+1
        i += 1

# lib_symbols: pin geometry per symbol name
i = src.index("(lib_symbols")
lib_block, lib_end = find_block(src, i)

pins_of = {}
for m in re.finditer(r'\(symbol "([^"]+)"\n', lib_block):
    name = m.group(1)
    if "_0_" in name or "_1_" in name:      # sub-unit graphic blocks
        continue
    blk, _ = find_block(lib_block, m.start())
    pl = []
    for pm in re.finditer(
        r'\(pin\s+\S+\s+\S+\s*\(at ([-\d.]+) ([-\d.]+) ([\d.]+)\)[\s\S]*?'
        r'\(number "([^"]+)"', blk):
        pl.append((pm.group(4), float(pm.group(1)), float(pm.group(2)), float(pm.group(3))))
    if pl:
        pins_of[name] = pl

# template instance per lib_id
templates = {}
for m in re.finditer(r'\n[ \t]*\(symbol \(lib_id "([^"]+)"\)', src):
    blk, _ = find_block(src, src.index("(symbol", m.start()))
    templates[m.group(1)] = blk

missing = [p["sym"] for p in PARTS.values() if p["sym"] not in templates]
if missing:
    sys.exit("no template for: " + ", ".join(sorted(set(missing))))

# ----------------------------------------------------------- layout --------
# One functional block per horizontal band, each block filling as many columns
# as it needs. Keeps related parts adjacent instead of scattering them.
ESP = "U1"
BLOCK_ORDER = [
    ("USB",   "USB-C input"),
    ("PWR",   "power path, charger, 3V3"),
    ("GAUGE", "battery sense + system I2C"),
    ("CAM",   "camera rails + DVP connector"),
    ("PANEL", "Sharp panel, 5V boost, hardware VCOM"),
    ("UI",    "shutter, buzzer, spare IO"),
    ("MCU",   "ESP32-S3 support"),
]
PITCH_X, PITCH_Y = 46.99, 40.64     # 37 and 32 times 1.27
X0, Y0 = 33.02, 60.96
ROWS = 9

pos, captions = {}, []
col = 0
for blk, title in BLOCK_ORDER:
    refs = sorted([r for r in PARTS if PARTS[r]["block"] == blk and r != ESP])
    ncols = max(1, -(-len(refs) // ROWS))
    captions.append((X0 + col * PITCH_X - 6.35, Y0 - 22.86, title))
    for n, ref in enumerate(refs):
        c, r = divmod(n, ROWS)
        pos[ref] = (X0 + (col + c) * PITCH_X, Y0 + r * PITCH_Y)
    col += ncols + 1                # one empty column between blocks

# the module goes in its own band underneath - it is 58mm tall with 41 labelled
# stubs, so it needs the room and would run off an A1 sheet on the right
pos[ESP] = (322.58, Y0 + (ROWS + 1.0) * PITCH_Y)
captions.append((papx := 241.30, Y0 + (ROWS + 1.0) * PITCH_Y - 45.72,
                 "ESP32-S3-WROOM-1-N16R8"))

def snap(v): return round(v / 1.27) * 1.27
pos = {r: (snap(x), snap(y)) for r, (x, y) in pos.items()}
for r, (x, y) in pos.items():
    assert abs(x / 1.27 - round(x / 1.27)) < 1e-6, r
    assert abs(y / 1.27 - round(y / 1.27)) < 1e-6, r

# ------------------------------------------------------------- emit --------
def uid(): return str(uuid.uuid4())

def set_prop(blk, key, val):
    pat = re.compile(r'(\(property "%s" )"[^"]*"' % re.escape(key))
    if pat.search(blk):
        return pat.sub(lambda m: m.group(1) + '"%s"' % val, blk, count=1)
    return blk

DIRS = {0: (-1, 0), 90: (0, 1), 180: (1, 0), 270: (0, -1)}
LBLROT = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}

sym_out, wire_out, lbl_out, nc_out = [], [], [], []
stub_len = 3.81
placed_pins = 0
label_count = 0

for ref, p in PARTS.items():
    X, Y = pos[ref]
    blk = templates[p["sym"]]
    # property (at ...) coordinates are absolute page coords, so the whole
    # block has to be translated, not just the symbol origin
    tm = re.search(r'\(at ([-\d.]+) ([-\d.]+) (\d+)\)', blk)
    tx, ty = float(tm.group(1)), float(tm.group(2))
    ddx, ddy = X - tx, Y - ty
    def _shift(m):
        return '(at %.2f %.2f %s)' % (float(m.group(1)) + ddx,
                                      float(m.group(2)) + ddy, m.group(3))
    blk = re.sub(r'\(at ([-\d.]+) ([-\d.]+) (\d+)\)', _shift, blk)
    blk = set_prop(blk, "Reference", ref)
    blk = set_prop(blk, "Value", p["val"])
    blk = set_prop(blk, "Footprint", p["fp"])
    blk = re.sub(r'\(uuid "[^"]*"\)', lambda m: '(uuid "%s")' % uid(), blk)
    # drop any stale instance path so KiCad re-annotates cleanly
    blk = re.sub(r'\(reference "[^"]*"\)', '(reference "%s")' % ref, blk)
    sym_out.append(blk)

    seen = {}
    for num, px, py, ang in pins_of.get(p["sym"], []):
        sx, sy = X + px, Y - py
        key = (round(sx, 2), round(sy, 2))
        net = CONN[ref].get(num, "__MISSING__")
        if key in seen:
            if seen[key] != net and net != "__MISSING__":
                sys.exit(f"{ref}: pins stacked at {key} carry different nets "
                         f"({seen[key]} vs {net})")
            continue
        seen[key] = net
        placed_pins += 1
        if net == "__MISSING__":
            sys.exit(f"{ref} pin {num} is not present in design.CONN")
        dx, dy = DIRS[int(ang) % 360]
        ex, ey = sx + dx * stub_len, sy + dy * stub_len
        if net is None:
            nc_out.append(f'  (no_connect (at {sx:.2f} {sy:.2f}) (uuid "{uid()}"))')
            continue
        wire_out.append(
            f'  (wire (pts (xy {sx:.2f} {sy:.2f}) (xy {ex:.2f} {ey:.2f}))\n'
            f'    (stroke (width 0) (type default)) (uuid "{uid()}"))')
        lbl_out.append(
            f'  (global_label "{net}" (shape passive) (at {ex:.2f} {ey:.2f} '
            f'{LBLROT[(dx,dy)]}) (fields_autoplaced yes)\n'
            f'    (effects (font (size 1.0 1.0))) (uuid "{uid()}"))')
        label_count += 1

# block captions
cap = []
for cx, cy, title in captions:
    cap.append(f'  (text "{title}" (at {cx:.2f} {cy:.2f} 0) (effects (font '
               f'(size 3.0 3.0) (bold yes))) (uuid "{uid()}"))')

body = "\n".join(sym_out) + "\n" + "\n".join(wire_out) + "\n" + \
       "\n".join(lbl_out) + "\n" + "\n".join(nc_out) + "\n" + "\n".join(cap) + "\n"

head = src[:lib_end]
head = head.replace('(paper "A4")', '(paper "A1")')
out = head + "\n" + body + '''
  (sheet_instances
    (path "/" (page "1"))
  )
)
'''
open(SCH, "w").write(out)
print(f"{len(PARTS)} symbols, {placed_pins} pin sites, {label_count} labels, "
      f"{len(nc_out)} no-connects")
