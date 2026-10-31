import base64, json, re, struct, zlib

W, H, B = 400, 240, 16
EYE_L_X, EYE_R_X, EYE_Y = 96, 240, 96
MOUTH_X, MOUTH_Y = 176, 144

def blank():
    return [bytearray(W) for _ in range(H)]

def block(px, x, y):
    for yy in range(y, y + B):
        for xx in range(x, x + B):
            px[yy][xx] = 1

def eye(px, x, y, happy):
    lo = y + B
    block(px, x, y if happy else lo)
    block(px, x + 3 * B, y if happy else lo)
    block(px, x + B, lo)
    block(px, x + 2 * B, lo)

def mouth(px, x, y, band):
    # Mirrors Face::mouth in firmware/src/display.cpp; the panel is the original.
    if band == 3:
        block(px, x - B, y)
        for i in range(3): block(px, x + i * B, y + B)
        block(px, x + 3 * B, y)
    elif band == 2:
        block(px, x + B, y)
    elif band == 1:
        for i in range(3): block(px, x + i * B, y)
    else:
        block(px, x, y + B); block(px, x + 2 * B, y + B); block(px, x + B, y)

def frame(band, breath_in):
    px = blank()
    ey = EYE_Y + (-4 if breath_in else 0)
    my = MOUTH_Y + (8 if breath_in else 0)
    eye(px, EYE_L_X, ey, band == 3)
    eye(px, EYE_R_X, ey, band == 3)
    mouth(px, MOUTH_X, my, band)
    return px

def png(px):
    stride = (W + 7) // 8
    raw = bytearray()
    for row in px:
        raw.append(0)
        line = bytearray(stride)
        for x in range(W):
            if row[x]: line[x >> 3] |= 128 >> (x & 7)
        raw += line
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 1, 0, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    out += chunk(b'IEND', b'')
    return 'data:image/png;base64,' + base64.b64encode(out).decode()

path = '/Users/meesboeijen/development/little-camera/site/camera-element.js'
src = open(path).read()
m = re.search(r'const SCREENS = (\{.*?\});\n', src, re.S)
screens = json.loads(m.group(1))

# `sleep` is the happy band, and `fine` is what the element calls content.
for band, name in ((3, 'sleep'), (2, 'fine'), (1, 'glum'), (0, 'sad')):
    screens[name + 'A'] = png(frame(band, False))
    screens[name + 'B'] = png(frame(band, True))

body = json.dumps(screens, separators=(',', ':'))
open(path, 'w').write(src[:m.start(1)] + body + src[m.end(1):])
print('ok', len(screens))
