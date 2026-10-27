# -*- coding: utf-8 -*-
"""Independent audit: re-derive the netlist from the GENERATED schematic and
compare it against design.CONN. Nothing is taken on trust from the generator."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS, CONN
CONN = {r: {str(k): v for k, v in d.items()} for r, d in CONN.items()}

SCH = sys.argv[1]
src = open(SCH).read()

def find_block(s, start):
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

i = src.index("(lib_symbols")
lib_block, lib_end = find_block(src, i)

pins_of = {}
for m in re.finditer(r'\(symbol "([^"]+)"\n', lib_block):
    name = m.group(1)
    if "_0_" in name or "_1_" in name: continue
    blk, _ = find_block(lib_block, m.start())
    pl = []
    for pm in re.finditer(
        r'\(pin\s+(\S+)\s+\S+\s*\(at ([-\d.]+) ([-\d.]+) ([\d.]+)\)[\s\S]*?\(number "([^"]+)"', blk):
        pl.append(dict(num=pm.group(5), etype=pm.group(1),
                       x=float(pm.group(2)), y=float(pm.group(3)), ang=float(pm.group(4))))
    if pl: pins_of[name] = pl

# instances
inst = {}
for m in re.finditer(r'\n\s*\(symbol \(lib_id "([^"]+)"\) \(at ([-\d.]+) ([-\d.]+) (\d+)\)', src):
    blk, _ = find_block(src, src.index("(symbol", m.start()))
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
    inst[ref] = dict(lib=m.group(1), x=float(m.group(2)), y=float(m.group(3)))

# wires and labels, keyed by endpoint
wires = [(float(a), float(b), float(c), float(d)) for a, b, c, d in
         re.findall(r'\(wire \(pts \(xy ([-\d.]+) ([-\d.]+)\) \(xy ([-\d.]+) ([-\d.]+)\)\)', src)]
labels = {}
for m in re.finditer(r'\(global_label "([^"]+)" \(shape \w+\) \(at ([-\d.]+) ([-\d.]+)', src):
    labels[(round(float(m.group(2)),2), round(float(m.group(3)),2))] = m.group(1)
ncs = set((round(float(a),2), round(float(b),2)) for a, b in
          re.findall(r'\(no_connect \(at ([-\d.]+) ([-\d.]+)\)', src))

wmap = {}
for x1,y1,x2,y2 in wires:
    wmap[(round(x1,2),round(y1,2))] = (round(x2,2),round(y2,2))

# rebuild: for each instance pin, follow stub -> label
derived = {}
etypes  = {}
for ref, info in inst.items():
    X, Y = info["x"], info["y"]
    seen = {}
    for p in pins_of.get(info["lib"], []):
        key = (round(X + p["x"], 2), round(Y - p["y"], 2))
        if key in seen: continue
        seen[key] = 1
        if key in ncs:
            derived.setdefault(ref, {})[p["num"]] = None
            continue
        end = wmap.get(key)
        net = labels.get(end) if end else None
        derived.setdefault(ref, {})[p["num"]] = net
        etypes.setdefault(net, []).append((ref, p["num"], p["etype"]))

# ---- compare against the declared table ---------------------------------
prob = []
for ref, pins in CONN.items():
    if ref not in derived:
        prob.append(f"{ref}: not placed in the schematic at all"); continue
    dd = derived[ref]
    stacked = {}
    for p in pins_of.get(PARTS[ref]["sym"], []):
        X, Y = inst[ref]["x"], inst[ref]["y"]
        stacked.setdefault((round(X+p["x"],2), round(Y-p["y"],2)), []).append(p["num"])
    rep = {v[0]: v for v in stacked.values()}
    for num, want in pins.items():
        primary = next((k for k, v in rep.items() if num in v), num)
        got = dd.get(primary, "ABSENT")
        if got != want:
            prob.append(f"{ref}.{num}: declared {want!r}, schematic has {got!r}")

print("=" * 66)
print("AUDIT 1 - schematic vs declared connection table")
print("=" * 66)
print(f"parts declared {len(CONN)}, placed {len(inst)}")
print(("MISMATCHES:\n  " + "\n  ".join(prob)) if prob else "no mismatches")

print()
print("=" * 66)
print("AUDIT 2 - every net needs a driver")
print("=" * 66)
bad = []
for net, pl in sorted(etypes.items()):
    if net is None: continue
    kinds = {e for _, _, e in pl}
    if "power_in" in kinds and not ({"power_out", "output"} & kinds):
        bad.append(f"{net}: power_in pins {[f'{r}.{n}' for r,n,e in pl if e=='power_in']} "
                   f"but no power_out on the net")
print("\n".join(bad) if bad else "every power_in net has a source")

print()
print("=" * 66)
print("AUDIT 3 - unconnected / suspicious")
print("=" * 66)
from collections import defaultdict
nets = defaultdict(list)
for ref, pins in derived.items():
    for n, net in pins.items():
        if net: nets[net].append(f"{ref}.{n}")
lonely = {n: v for n, v in nets.items() if len(v) < 2}
print("single-pin nets:", lonely if lonely else "none")
print(f"total nets: {len(nets)}")
