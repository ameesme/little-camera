import base64, json, re, struct, zlib

W, H = 400, 240
GLYPH = [
    '#...#',
    '#...#',
    '.#.#.',
    '..#..',
    '.#.#.',
    '#...#',
    '#...#',
]
BLOCK = 10
GAP = 14
COUNT = 6

def panel():
    px = [[0] * W for _ in range(H)]
    gw, gh = len(GLYPH[0]) * BLOCK, len(GLYPH) * BLOCK
    total = COUNT * gw + (COUNT - 1) * GAP
    x0, y0 = (W - total) // 2, (H - gh) // 2
    for i in range(COUNT):
        left = x0 + i * (gw + GAP)
        for row, line in enumerate(GLYPH):
            for col, cell in enumerate(line):
                if cell != '#':
                    continue
                for y in range(y0 + row * BLOCK, y0 + row * BLOCK + BLOCK):
                    for x in range(left + col * BLOCK, left + col * BLOCK + BLOCK):
                        px[y][x] = 1
    return px

def png(px):
    stride = (W + 7) // 8
    raw = bytearray()
    for row in px:
        raw.append(0)
        line = bytearray(stride)
        for x in range(W):
            if row[x]:
                line[x >> 3] |= 128 >> (x & 7)
        raw += line
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return 'data:image/png;base64,' + base64.b64encode(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 1, 0, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
        + chunk(b'IEND', b'')
    ).decode()

grid = panel()
for y in range(0, H, 8):
    print(''.join('#' if grid[y][x] else '.' for x in range(0, W, 8)))

path = '/Users/meesboeijen/development/little-camera/site/camera-element.js'
js = open(path).read()
m = re.search(r'const SCREENS = (\{.*?\});\n', js, re.S)
screens = json.loads(m.group(1))
screens['passkey'] = png(grid)
order = ['sleepA', 'sleepB', 'fineA', 'fineB', 'glumA', 'glumB', 'sadA', 'sadB',
         'viewfinder', 'gallery', 'save', 'mail', 'send', 'images', 'camera', 'sparkles', 'passkey']
body = json.dumps({k: screens[k] for k in order}, separators=(',', ':'))
open(path, 'w').write(js[:m.start(1)] + body + js[m.end(1):])
print('wrote passkey')
