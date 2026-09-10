"""Prepare a local static preview from the user's FBX; never modify the download."""
from pathlib import Path
import argparse
import re
import subprocess
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--assimp-prefix", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    models = list((source / "source").glob("*.fbx"))
    if len(models) != 1:
        raise SystemExit("Expected exactly one FBX under source/source")
    assets = HERE / "assets"
    assets.mkdir(exist_ok=True)
    build = assets / ".prepare"
    wrapper = str(ROOT / "cmake/with-msvc.cmd")
    subprocess.run([wrapper, "cmake.exe", "-S", str(HERE / "tools"), "-B", str(build),
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                    f"-DASSIMP_PREFIX={args.assimp_prefix.resolve().as_posix()}"], check=True)
    subprocess.run([wrapper, "cmake.exe", "--build", str(build)], check=True)
    subprocess.run([str(build / "anime_asset_prepare.exe"), str(models[0]),
                    str(assets / "witch.obj")], check=True)
    # Decode the source PNGs to the engine's supported RGBA8 upload contract.
    # In particular hairs.png is 16-bit. Keep authored UVs, dimensions and paint.
    for texture in (source / "textures").iterdir():
        if texture.suffix.lower() in {".png", ".jpg", ".jpeg"}:
            with Image.open(texture) as image:
                image.convert("RGBA").save(assets / (texture.stem + ".png"))
    aliases = {"red Diffuse Color.png": "red_Diffuse_Color.png",
               "hat and boots2.png": "hat_and_boots2.png", "w.jpg": "w.png"}
    material = assets / "witch.mtl"
    text = material.read_text(encoding="utf-8")

    def resolve(match):
        basename = match[1].replace("\\", "/").split("/")[-1].strip()
        filename = aliases.get(basename, basename)
        if not (assets / filename).is_file():
            raise ValueError(f"Missing source texture: {filename}")
        return "map_Kd " + filename

    material.write_text(re.sub(r"map_Kd ([^\n]+)", resolve, text), encoding="utf-8")
    print("Prepared static preview:", assets / "witch.obj")


if __name__ == "__main__":
    main()
