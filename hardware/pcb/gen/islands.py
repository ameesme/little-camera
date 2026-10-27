# print the copper items (segment endpoints, vias, pads) of each island of a net
import sys, math, os
from collections import defaultdict
PCB, NET = sys.argv[1], sys.argv[2]
sys.argv = ['conncheck', PCB]
G = {}
code = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'conncheck.py')).read()
exec(code.split('# ---------------------------------------------------------------- report ---')[0], G)
pads, segs, vias, f, bynet = G['pads'], G['segs'], G['vias'], G['f'], G['bynet']
g = bynet[NET]; isl = defaultdict(list)
for i in g['p']: isl[f(('p',i))].append(('pad', pads[i]['ref']+'.'+pads[i]['num'], round(pads[i]['x'],3), round(pads[i]['y'],3), pads[i]['layers'][0]))
for i in g['s']:
    s = segs[i]; isl[f(('s',i))] += [('seg', s['layer'], round(s['x1'],3), round(s['y1'],3), s['w']), ('seg', s['layer'], round(s['x2'],3), round(s['y2'],3), s['w'])]
for i in g['v']: isl[f(('v',i))].append(('via', '', round(vias[i]['x'],3), round(vias[i]['y'],3), ''))
items = [v for v in isl.values() if any(t[0]=='pad' for t in v)] + [v for v in isl.values() if not any(t[0]=='pad' for t in v)]
for k, it in enumerate(items):
    print('ISLAND', k, len(it))
    for x in sorted(set(it), key=lambda t: (t[2], t[3])): print('  ', x)
