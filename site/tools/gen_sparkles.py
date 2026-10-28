import base64, json, re, struct, zlib

W, H, SCALE = 400, 240, 7
SIZE = 24

def star(grid, cx, cy, r):
    # Astroid: |dx|^(2/3) + |dy|^(2/3) <= r^(2/3) is the four-pointed star Lucide draws.
    p = 2.0 / 3.0
    for y in range(SIZE):
        for x in range(SIZE):
            dx, dy = abs(x - cx), abs(y - cy)
            if dx ** p + dy ** p <= r ** p + 0.2:
                grid[y][x] = 1

def cross(grid, cx, cy, arm):
    for d in range(-arm, arm + 1):
        grid[cy + d][cx] = 1
        grid[cy][cx + d] = 1
    for d in (-1, 0, 1):
        grid[cy + d][cx] = 1
        grid[cy][cx + d] = 1

def sparkles():
    g = [[0] * SIZE for _ in range(SIZE)]
    star(g, 9.5, 12.5, 8.5)
    cross(g, 19, 5, 3)
    cross(g, 19, 19, 3)
    return g

icon = sparkles()
for row in icon:
    print(''.join('#' if c else '.' for c in row))

def png(icon):
    size = SIZE * SCALE
    x0, y0 = (W - size) // 2, (H - size) // 2
    stride = (W + 7) // 8
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        line = bytearray(stride)
        if y0 <= y < y0 + size:
            row = icon[(y - y0) // SCALE]
            for x in range(size):
                if row[x // SCALE]:
                    px = x0 + x
                    line[px >> 3] |= 128 >> (px & 7)
        raw += line
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return 'data:image/png;base64,' + base64.b64encode(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 1, 0, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
        + chunk(b'IEND', b'')
    ).decode()

path = '/Users/meesboeijen/development/little-camera/site/camera-element.js'
js = open(path).read()
m = re.search(r'const SCREENS = (\{.*?\});\n', js, re.S)
screens = json.loads(m.group(1))
screens['sparkles'] = png(icon)
order = ['sleepA', 'sleepB', 'fineA', 'fineB', 'glumA', 'glumB', 'sadA', 'sadB',
         'viewfinder', 'gallery', 'save', 'mail', 'send', 'images', 'camera', 'sparkles']
body = json.dumps({k: screens[k] for k in order}, separators=(',', ':'))
open(path, 'w').write(js[:m.start(1)] + body + js[m.end(1):])
print('wrote sparkles')
