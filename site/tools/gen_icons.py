import base64, json, re, struct, zlib

W, H, SCALE = 400, 240, 7
HEADER = '/Users/meesboeijen/development/little-camera/firmware/src/icons.h'
WANT = {'MAIL': 'mail', 'SEND': 'send', 'IMAGES': 'images', 'CAMERA': 'camera',
        'SPARKLES': 'sparkles', 'USER': 'person', 'ALERT': 'alert', 'BLOG': 'blog',
        'BATTERY_LOW': 'battery'}

src = open(HEADER).read()

def bitmap(name):
    m = re.search(r'static const uint8_t %s\[24 \* 3\] = \{(.*?)\};' % name, src, re.S)
    bytes_ = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]
    return [[(bytes_[y * 3 + (x >> 3)] >> (7 - (x & 7))) & 1 for x in range(24)] for y in range(24)]

def png(icon):
    size = 24 * SCALE
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

icons = {key: bitmap(name) for name, key in WANT.items()}
for key, icon in icons.items():
    print(key)
    for row in icon:
        print('  ' + ''.join('#' if c else '.' for c in row))

path = '/Users/meesboeijen/development/little-camera/site/camera-element.js'
js = open(path).read()
m = re.search(r'const SCREENS = (\{.*?\});\n', js, re.S)
screens = json.loads(m.group(1))
for key, icon in icons.items():
    screens[key] = png(icon)
body = json.dumps(screens, separators=(',', ':'))
open(path, 'w').write(js[:m.start(1)] + body + js[m.end(1):])
print('wrote', list(icons))
