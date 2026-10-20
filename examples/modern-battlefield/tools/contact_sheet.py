"""Lay out unmodified engine screenshots with labels; originals remain beside it."""
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


def main() -> None:
    root = Path(__file__).resolve().parents[1] / "screenshots"
    models = [
        ("tank", "01  MAIN BATTLE TANK"),
        ("tank-destroyer", "02  TANK DESTROYER"),
        ("self-propelled-artillery", "03  SELF-PROPELLED HOWITZER"),
        ("self-propelled-mortar", "04  MORTAR CARRIER"),
        ("armored-car", "05  8x8 ARMORED VEHICLE"),
        ("anti-tank-gun", "06  ANTI-TANK GUN"),
        ("howitzer", "07  FIELD HOWITZER"),
        ("heavy-howitzer", "08  HEAVY HOWITZER"),
        ("mortar", "09  INFANTRY MORTAR"),
        ("machine-gun", "10  HEAVY MACHINE GUN"),
        ("helicopter", "11  ATTACK HELICOPTER"),
        ("attack-aircraft", "12  CLOSE-SUPPORT JET"),
        ("bomber-top", "13  FOUR-ENGINE TURBOPROP BOMBER"),
        ("medium-fighter", "14  TWIN-ENGINE FIGHTER"),
    ]
    fortifications = [
        ("sandbags", "15  SANDBAG WALL"),
        ("wooden-crate", "16  WOODEN SHIPPING CRATE"),
        ("hedgehog", "17  STEEL HEDGEHOG"),
        ("barbed-wire", "18  CONCERTINA BARBED WIRE"),
        ("concrete-post", "19  REINFORCED CONCRETE POST"),
        ("ammunition-box", "20  AMMUNITION CAN"),
        ("medical-box", "21  MEDICAL CASE"),
        ("bunker", "22  CONCRETE PILLBOX"),
        ("machine-gun-position", "23  MACHINE-GUN EMPLACEMENT"),
        ("artillery-position", "24  ARTILLERY EMPLACEMENT"),
    ]
    font = ImageFont.load_default(size=19)
    title_font = ImageFont.load_default(size=26)

    def sheet(items: list, name: str, columns: int, tile_width: int) -> None:
        tile_height = round(tile_width * 1000 / 1440)
        label_height, gutter, header = 42, 12, 74
        rows = (len(items) + columns - 1) // columns
        canvas = Image.new("RGB", (columns * (tile_width + gutter) + gutter,
                                   rows * (tile_height + label_height + gutter) + header), "#121c27")
        draw = ImageDraw.Draw(canvas)
        draw.text((16, 12), "MODERN BATTLEFIELD / INDIVIDUAL MODEL REVIEW", font=title_font, fill="#e0e8ed")
        draw.text((16, 46), "Actual EVEngine frames - original 1440 x 1000 PNGs retained separately", font=font, fill="#9fbbc8")
        for index, (kind, label) in enumerate(items):
            x = gutter + (index % columns) * (tile_width + gutter)
            y = header + (index // columns) * (tile_height + label_height + gutter)
            with Image.open(root / f"{kind}.png") as source:
                canvas.paste(source.convert("RGB").resize((tile_width, tile_height), Image.Resampling.LANCZOS), (x, y))
            draw.text((x + 10, y + tile_height + 10), label, font=font, fill="#d4e2e9")
        canvas.save(root / name)
        print(root / name)

    sheet(models + fortifications, "all-models.png", 4, 480)
    sheet(models[10:], "aircraft.png", 2, 800)
    sheet(fortifications, "fortifications.png", 4, 480)
    sheet([fortifications[7], fortifications[8], ("artillery-position-top", "24  ARTILLERY / TOP VIEW")],
          "positions.png", 3, 640)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
