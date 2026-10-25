"""Bake deterministic parent/child ink trajectories; requires Pillow. No runtime simulation."""
from pathlib import Path
import math
import random
import sys

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parent
COLS, ROWS, WIDTH, HEIGHT, FRAMES, FPS = 8, 5, 384, 384, 40, 30
SCALE = 3
# Crop to the splash region instead of spending texels on the empty left half.
MIN_X, MIN_Y, SPAN_X, SPAN_Y = -0.20, -1.10, 1.85, 2.10


def position(p, age):
    return (p['x'] + p['vx'] * age, p['y'] + p['vy'] * age + 0.12 * age * age)


def bake():
    rng = random.Random(73129)
    particles = []
    for i in range(16):
        a = -0.75 + (i + rng.uniform(0.15, 0.85)) / 16 * 1.85
        birth = 0.025 + (2.85 - a) / 4.28 * 0.24
        speed = rng.uniform(0.22, 0.38)
        tangent = (1.17 * math.sin(a), -0.68 * math.cos(a))
        norm = math.hypot(*tangent)
        spread = rng.uniform(0.97, 1.13)
        parent = dict(x=-0.22 + math.cos(a) * 1.17 * spread,
                      y=-0.12 + math.sin(a) * 0.68 * spread,
                      vx=tangent[0] / norm * speed + math.cos(a) * 0.10,
                      vy=tangent[1] / norm * speed + math.sin(a) * 0.10,
                      birth=birth, life=0.72, radius=rng.uniform(0.017, 0.030),
                      split=rng.uniform(0.13, 0.22), alpha=rng.uniform(0.88, 1.0), phase=rng.uniform(0, 6.28))
        particles.append(parent)
        # Children inherit the parent's position and velocity at breakup, not its emitter origin.
        sx, sy = position(parent, parent['split'])
        for _ in range(rng.randint(4, 6)):
            direction = math.atan2(parent['vy'], parent['vx']) + rng.uniform(-0.48, 0.48)
            burst = rng.uniform(0.30, 0.58)
            particles.append(dict(x=sx, y=sy, vx=parent['vx'] + math.cos(direction) * burst,
                                  vy=parent['vy'] + 0.24 * parent['split'] + math.sin(direction) * burst,
                                  birth=birth + parent['split'], life=rng.uniform(0.30, 0.46),
                                  radius=rng.uniform(0.0045, 0.0085), split=100.0,
                                  alpha=rng.uniform(0.82, 0.98), phase=rng.uniform(0, 6.28)))
    atlas = Image.new('RGBA', (WIDTH * COLS, HEIGHT * ROWS))
    for frame in range(FRAMES):
        mask = Image.new('L', (WIDTH * SCALE, HEIGHT * SCALE))
        for p in particles:
            age = frame / FPS - p['birth']
            if age <= 0 or age >= p['life']:
                continue
            x, y = position(p, age)
            fade = min(age / 0.035, 1) * min((p['life'] - age) / 0.18, 1)
            # Parent loses volume when shedding children; they continue independently.
            radius = p['radius'] * (0.78 if age > p['split'] else 1.0)
            heading = math.atan2(p['vy'] + 0.24 * age, p['vx'])
            stretch = 1.2 + math.hypot(p['vx'], p['vy']) * 0.9
            points = []
            for j in range(20):
                theta = j * math.tau / 20
                rough = 1 + 0.13 * math.sin(theta * 5 + p['phase'])
                dx, dy = math.cos(theta) * radius * stretch * rough, math.sin(theta) * radius * rough
                px = x + dx * math.cos(heading) - dy * math.sin(heading)
                py = y + dx * math.sin(heading) + dy * math.cos(heading)
                points.append((((px - MIN_X) / SPAN_X) * WIDTH * SCALE, ((py - MIN_Y) / SPAN_Y) * HEIGHT * SCALE))
            layer = Image.new('L', mask.size)
            ImageDraw.Draw(layer).polygon(points, fill=round(255 * fade * p['alpha']))
            # A newly born translucent child must never erase the opaque parent.
            mask = ImageChops.lighter(mask, layer)
        img = Image.new('RGBA', (WIDTH, HEIGHT), (255, 255, 255, 0))
        img.putalpha(mask.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS))
        atlas.paste(img, ((frame % COLS) * WIDTH, (frame // COLS) * HEIGHT))
    assert atlas.getchannel('A').getextrema()[1] > 0
    assert atlas.crop((0, 0, WIDTH, HEIGHT)).getchannel('A').getbbox() is None
    assert atlas.crop((7 * WIDTH, 4 * HEIGHT, 8 * WIDTH, 5 * HEIGHT)).getchannel('A').getbbox() is None
    atlas.save(ROOT / 'assets' / 'splash-flipbook.png')
    print(f'Baked {len(particles)} parent/child trajectories, {FRAMES} frames at {FPS} fps; {atlas.size}')


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    bake()
