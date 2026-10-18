"""Generate matching OpenSCAD geometry, scene description and physically based textures."""

from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw

from design import PARTS, PALETTE, JOINTS

ROOT = Path(__file__).resolve().parent


def main():
    scad = [
        "// IRONWARDEN / RX-78-2 reference study | units mm | Z up | front -Y",
        "// Generated from build_model.py. Open with OpenSCAD, F5 preview / F6 render.",
        "$fn=40;",
        "show_weathering=true;",
        "model_scale=1;",
        'part="all";',
        "module chamfer_box(s,b) { hull() for(x=[-1,1],y=[-1,1],z=[-1,1]) translate([x*(s[0]/2-b),y*(s[1]/2-b),z*(s[2]/2-b)]) sphere(r=b,$fn=6); }",
        "scale(model_scale) {",
    ]
    for i, p in enumerate(PARTS):
        cond = f'(part=="all" || part=="{p["name"]}")'
        if p["name"] == "edge_paint_chip":
            cond += " && show_weathering"
        if p["kind"] == "mesh":
            prim = f"polyhedron(points={p['vertices']},faces={p['faces']},convexity=8);"
        elif p["kind"] == "box":
            prim = f"chamfer_box({p['size']},{p['bevel']});"
        else:
            prim = f"cylinder(r1={p['size'][0]},r2={p['topRadius']},h={p['size'][1]},center=true);"
        scad.append(
            f"// {i}: {p['name']}\nif ({cond}) color({PALETTE[p['mat']]}) translate({list(p['pos'])}) rotate({list(p['rot'])}) {prim}"
        )
    scad.append("}")
    (ROOT / "ironwarden.scad").write_text("\n".join(scad), encoding="utf-8")
    (ROOT / "scene.json").write_text(
        json.dumps(
            {
                "schema": "ironwarden-geometry",
                "version": 2,
                "parts": PARTS,
                "palette": PALETTE,
                "joints": JOINTS,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    (ROOT / "model_info.nut").write_text(f"wardPartCount <- {len(PARTS)};\n", encoding="utf-8")
    textures = ROOT / "textures"
    textures.mkdir(exist_ok=True)
    rng = np.random.default_rng(417)
    n = 1024

    def noise(k):
        im = Image.fromarray((rng.random((k, k)) * 255).astype("uint8"))
        return np.asarray(im.resize((n, n), Image.Resampling.BICUBIC), dtype=float) / 255

    fine = noise(240)
    grain = noise(1024)
    for name, color in PALETTE.items():
        yy, xx = np.mgrid[0:n, 0:n] / (n - 1)
        edge = np.maximum(np.abs(xx - 0.5), np.abs(yy - 0.5)) * 2
        wear = np.clip((edge - 0.93) * 12, 0, 1) * np.clip((fine - 0.68) * 4, 0, 1)
        mask = wear * 0.08
        if name in ["chrome", "cyan", "rubber"]:
            wear *= 0.1
            mask *= 0
        if name == "rust":
            mask = np.clip(0.8 + fine * 0.2, 0, 1)
        base = np.array(color)[None, None, :] * (
            0.95 + 0.035 * fine[:, :, None] + 0.015 * grain[:, :, None]
        )
        metal_chips = np.stack(
            [0.25 + 0.04 * fine, 0.27 + 0.04 * fine, 0.29 + 0.04 * fine], axis=-1
        )
        base = base * (1 - wear[:, :, None]) + metal_chips * wear[:, :, None]
        rust = np.stack([0.23 + 0.06 * fine, 0.08 + 0.035 * fine, 0.025 + 0.015 * fine], axis=-1)
        base = base * (1 - mask[:, :, None]) + rust * mask[:, :, None]
        dirt = np.clip((noise(20) - 0.60) * 0.35, 0, 0.10)
        if name in ["white", "blue", "red", "yellow", "grey"]:
            dirt *= 0.25
        if name in ["dark", "steel", "rubber"]:
            dirt *= 2.0
        base *= 1 - dirt[:, :, None]
        image = Image.fromarray(np.uint8(np.clip(base, 0, 1) * 255))
        draw = ImageDraw.Draw(image)
        if name not in ["cyan", "rubber"]:
            for _ in range(42):
                x, y = rng.integers(0, n, 2)
                length = int(rng.integers(2, 21))
                col = tuple(int(v * 255 * 0.72) for v in color)
                draw.line(
                    (int(x), int(y), int(x + length), int(y + length * 0.12)), fill=col, width=1
                )
        image.save(textures / f"{name}_basecolor.png")
        rough = np.clip(
            (0.23 if name == "chrome" else 0.40) + mask * 0.33 + dirt * 0.4 + (fine - 0.5) * 0.17,
            0.12,
            0.98,
        )
        metal = np.full(
            (n, n), 0.85 if name in ["steel", "chrome", "brass", "fastener"] else 0.08
        ) * (1 - mask)
        height = fine * 0.015 + grain * 0.004 + wear * 0.22
        dy, dx = np.gradient(height)
        normal = np.stack([-dx * 3, -dy * 3, np.ones_like(dx)], axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        for suffix, arr in [
            ("roughness", rough),
            ("metallic", metal),
            ("rustmask", mask),
            ("height", height),
        ]:
            Image.fromarray(np.uint8(np.clip(arr, 0, 1) * 255)).save(
                textures / f"{name}_{suffix}.png"
            )
        Image.fromarray(np.uint8((normal * 0.5 + 0.5) * 255)).save(textures / f"{name}_normal.png")
    print(f"Generated {len(PARTS)} parts and {len(PALETTE) * 6} texture maps.")


if __name__ == "__main__":
    main()
