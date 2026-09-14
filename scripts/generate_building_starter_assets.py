from pathlib import Path
from PIL import Image, ImageDraw
import random
root = Path('examples')
out = root / 'building' / 'starter'
out.mkdir(exist_ok=True)
for kind, count in [('grass', 8), ('road', 6)]:
    for n in range(count):
        im = Image.new('RGBA', (32, 32), (89, 117, 54, 255))
        d = ImageDraw.Draw(im)
        rng = random.Random(n)
        for i in range(40):
            x, y = (rng.randrange(32), rng.randrange(32))
            d.point((x, y), fill=(116, 140, 69, 255))
        if kind == 'road':
            for y in range(0, 32, 8):
                for x in range(0, 32, 8):
                    d.rounded_rectangle((x + 1, y + 1, x + 7, y + 7), radius=2, fill=(139 + n * 3, 137 + n * 3, 119 + n * 3, 255))
        im.save(out / f'{kind}{n}.png')
for name in ['house', 'stall', 'dock', 'crate', 'barrel']:
    im = Image.new('RGBA', (32, 32))
    d = ImageDraw.Draw(im)
    d.rectangle((3, 10, 29, 30), fill=(144, 109, 65, 255), outline=(66, 48, 31, 255))
    if name == 'house':
        d.polygon([(0, 13), (16, 0), (31, 13)], fill=(123, 64, 39, 255))
        d.rectangle((13, 20, 19, 30), fill=(51, 38, 26, 255))
    else:
        for y in range(12, 30, 4):
            d.line((4, y, 28, y), fill=(72, 51, 33, 255))
    im.save(out / (name + '.png'))
Image.new('RGBA', (1, 1), 'white').save(out / 'pixel.png')
out = root / 'building-tilemap' / 'starter'
out.mkdir(exist_ok=True)
files = [n for n in range(121) if n != 66]
variants = [11, 12, 13, 2, 3, 37, 69, 72, 73, 7, 8, 10, 14, 108, 114, 116]
allfiles = files + [n for n in variants for _ in range(4)]
a = Image.new('RGBA', (640, (len(allfiles) + 9) // 10 * 46))
for i, n in enumerate(allfiles):
    c = (74, 110, 49, 255)
    if n in [2, 3, 37, 69, 72, 73]:
        c = (145, 137, 112, 255)
    if n in [7, 8, 10]:
        c = (179, 151, 97, 255)
    if n in [14, 108]:
        c = (46, 117, 145, 255)
    if n in [114, 116]:
        c = (32, 71, 111, 255)
    im = Image.new('RGBA', (64, 46))
    d = ImageDraw.Draw(im)
    d.polygon([(32, 0), (63, 23), (32, 45), (0, 23)], fill=c)
    rng = random.Random(i)
    for k in range(25):
        x, y = (rng.randrange(64), rng.randrange(46))
        if im.getpixel((x, y))[3]:
            d.point((x, y), fill=tuple((min(255, v + 12) for v in c[:3])) + (255,))
    a.paste(im, (i % 10 * 64, i // 10 * 46))
a.save(out / 'c14-atlas.png')
im = Image.new('RGBA', (64, 46))
ImageDraw.Draw(im).polygon([(32, 0), (63, 23), (32, 45), (0, 23)], fill=(255, 255, 255, 24), outline='white', width=2)
im.save(out / 'cursor.png')
mapping = {n: i + 1 for i, n in enumerate(files)}
v = {n: list(range(121 + i * 4, 125 + i * 4)) for i, n in enumerate(variants)}
(out / 'catalog.nut').write_text('c14Gids <- {' + ','.join(('[%d]=%d' % p for p in mapping.items())) + '};\nc14Variants <- {' + ','.join(('[%d]=[%s]' % (n, ','.join(map(str, g))) for n, g in v.items())) + '};\n')
