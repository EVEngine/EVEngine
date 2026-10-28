"""Compose an animated preview from the final atlases without altering character art."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent / "assets"
atlases = [Image.open(root / name / "64/final/walk-sheet-clean.png").convert("RGBA")
           for name in ("hero", "scholar", "guard")]
frames = []
for index in range(6):
    frame = Image.new("RGB", (768, 576), "#18232d")
    for actor, atlas in enumerate(atlases):
        for direction in range(4):
            tile = atlas.crop((index * 64, direction * 64, index * 64 + 64, direction * 64 + 64))
            tile = tile.resize((192, 192), Image.Resampling.NEAREST)
            frame.paste(tile, (direction * 192, actor * 192), tile)
    frames.append(frame)
frames[0].save(root / "cast-preview.webp", save_all=True, append_images=frames[1:], duration=110, loop=0, lossless=True)
frames[0].save(root / "cast-preview.gif", save_all=True, append_images=frames[1:], duration=110, loop=0, disposal=2)
