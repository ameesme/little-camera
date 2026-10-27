#!/usr/bin/env python3
"""AUDIT 4 - board footprint pad nets vs the declared connection table.

Closes the last gap in the chain:
  design.CONN  ->  schematic   (gen/audit.py)
  design.CONN  ->  board pads  (this script)
  board pads   ->  copper      (gen/conncheck.py)
"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import CONN, PARTS

PCB = sys.argv[1] if len(sys.argv) > 1 else 'little_camera.kicad_pcb'
s = open(PCB).read()

def close(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1
    raise ValueError

board = {}          # refdes -> {pad number -> set(nets)}
for m in re.finditer(r'\n\t\(footprint "', s):
    i = m.start() + 1
    blk = s[i:close(s, i)]
    rm = re.search(r'\(property "Reference" "([^"]+)"', blk)
    if not rm: continue
    ref = rm.group(1)
    d = board.setdefault(ref, {})
    for pm in re.finditer(r'\(pad "', blk):
        g = blk[pm.start():close(blk, pm.start())]
        num = re.match(r'\(pad "([^"]*)"', g).group(1)
        net = re.search(r'\(net "([^"]*)"\)', g)
        d.setdefault(num, set()).add(net.group(1) if net else '')

bad = 0
missing_fp = []
virtual = [r for r, d in PARTS.items() if not d.get('fp')]   # PWR_FLAG etc: schematic only
for ref, pins in CONN.items():
    if ref in virtual: continue
    if ref not in board:
        missing_fp.append(ref); continue
    for pin, want in pins.items():
        got = board[ref].get(str(pin))
        if got is None:
            print(f'  MISSING PAD  {ref}.{pin}  (expect {want})'); bad += 1; continue
        if len(got) > 1:
            print(f'  SPLIT PAD    {ref}.{pin}  pads carry {sorted(got)}'); bad += 1; continue
        have = next(iter(got))
        if want in (None, '', 'NC'):
            if have not in ('', None):
                print(f'  NC PAD WIRED {ref}.{pin}  -> {have}'); bad += 1
        elif have != want:
            print(f'  WRONG NET    {ref}.{pin}  board={have!r} expected={want!r}'); bad += 1

# board pads carrying a net that the design never declared for that pin
extra = 0
for ref, pads in board.items():
    if ref not in CONN:
        if any(n for ns in pads.values() for n in ns if n):
            print(f'  UNDECLARED PART WITH NETS: {ref}'); extra += 1
        continue
    for num, nets in pads.items():
        if str(num) in (str(k) for k in CONN[ref]): continue
        for n in nets:
            if n:
                print(f'  EXTRA NETTED PAD {ref}.{num} -> {n}'); extra += 1

print()
print(f'parts in table: {len(CONN)}   footprints on board: {len(board)}')
print(f'pins checked:   {sum(len(v) for r, v in CONN.items() if r not in virtual)}')
print(f'schematic-only symbols skipped: {sorted(virtual)}')
if missing_fp: print('FOOTPRINT MISSING FROM BOARD:', missing_fp)
print(f'mismatches: {bad}   extras: {extra}')
print('RESULT:', 'PASS - board pad nets match the declared design exactly' if (bad == 0 and extra == 0 and not missing_fp) else 'FAIL')
