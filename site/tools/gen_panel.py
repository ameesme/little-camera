import base64, json, re, struct, sys, zlib

W, H = 400, 240
ELEMENT = '/Users/meesboeijen/development/little-camera/site/camera-element.js'

def read_bmp(path):
    data = open(path, 'rb').read()
    start = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    if (width, height) != (W, H):
        raise SystemExit('expected a %dx%d panel, got %dx%d' % (W, H, width, height))
    stride = (W * 3 + 3) & ~3
    rows = []
    for y in range(H):
        off = start + (H - 1 - y) * stride
        rows.append([1 if data[off + x * 3] > 127 else 0 for x in range(W)])
    return rows

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

key, bmp = sys.argv[1], sys.argv[2]
grid = read_bmp(bmp)
for y in range(0, H, 8):
    print(''.join('#' if grid[y][x] else '.' for x in range(0, W, 8)))

js = open(ELEMENT).read()
m = re.search(r'const SCREENS = (\{.*?\});\n', js, re.S)
screens = json.loads(m.group(1))
screens[key] = png(grid)
body = json.dumps(screens, separators=(',', ':'))
open(ELEMENT, 'w').write(js[:m.start(1)] + body + js[m.end(1):])
print('wrote', key)
