"""Prepare and publish native optional-road-component render evidence (not Unity parity)."""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import re
import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import road_parity
import utf8_stdio

CASES = {
    "crowned-section": "拱形路面与异材质截面",
    "concave-beam": "闭合截面与梁体",
    "bridge-tunnel": "桥面、桥墩与隧道区间",
    "continuous-rail": "连续路侧护栏",
    "custom-connection": "自定义接入口与护栏",
    "prefab-deformation": "弯坡 prefab 围栏变形",
    "multi-arm-facilities": "多臂路口连续设施衔接",
    "terrain-multichannel": "地形贴合与材质通道",
    "prefab-auto-spacing": "prefab 自动间距与平滑组",
    "tunnel-portals": "隧道入口开孔与覆盖地形",
    "shared-presets": "共享预设、步道与设施覆盖",
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(args: argparse.Namespace) -> None:
    source = args.root / "source"
    subprocess.run([str(args.generator.resolve()), str(source.resolve())], check=True)
    material = args.material_root
    texture = args.project / "Assets/EasyRoads3D/textures/roads"
    for case in CASES:
        original = (source / case / "main.nut").read_text(encoding="utf-8")
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            game.mkdir(parents=True, exist_ok=True)
            for name in ("mesh_data.nut", "config.nut"):
                shutil.copyfile(source / case / name, game / name)
            shutil.copyfile(material / "road-asphalt.frag.spv", game / "road-asphalt.frag.spv")
            shutil.copyfile(texture / "roadsample_d.jpg", game / "road-color.jpg")
            shutil.copyfile(texture / "road_b.jpg", game / "road-normal.jpg")
            Image.new("RGB", (4, 4), (128, 128, 255)).save(game / "flat-normal.png")
            initial = """
persist component_shader=null;
persist component_textures=[];
persist component_materials=[];
persist component_meshes=[];
function eve_init() {
 local rc=gfx.getRenderControl();rc.disable("gbuffer");rc.disable("gpuDriven");rc.compile();
 local tone=gfx.setSceneToneMapping("none");if(!tone.ok)throw "component tone rejected";
 local loaded=gfx.loadMeshShaderSpv("","road-asphalt.frag.spv");if(!loaded.ok)throw "component shader rejected";
 component_shader=loaded.value;
 local surface=gfx.configureMeshShaderSurface(component_shader,"opaque",true,false);
 if(!surface.ok)throw "component raster state rejected";
 foreach(path in ["road-color.jpg","road-normal.jpg","flat-normal.png"]) {
  local texture=gfx.newTextureFromFileRepeated(path,false,false);
  if(texture==null)throw "component texture missing";component_textures.append(texture);
 }
"""
            script = original.replace("function eve_init() {", initial)
            count = 0

            def replace(match: re.Match[str]) -> str:
                nonlocal count
                color = tuple(float(x) for x in match[1].split(","))
                asphalt = color == (0.25, 0.29, 0.35)
                filename = f"group-{count}.png"
                Image.new("RGB", (4, 4), tuple(round(c * 255) for c in color)).save(game / filename)
                count += 1
                albedo = (
                    "component_textures[0]"
                    if asphalt
                    else f'gfx.newTextureFromFileRepeated("{filename}",false,false)'
                )
                return (
                    "material.setTint(1.0,1.0,1.0,1.0);material.setShader(component_shader);"
                    f'local albedo={albedo};if(albedo==null)throw "group texture missing";'
                    "component_textures.append(albedo);material.setAlbedoTexture(albedo);"
                    f"material.setNormalTexture(component_textures[{1 if asphalt else 2}]);"
                    "component_materials.append(material);component_meshes.append(mesh);"
                )

            script = re.sub(
                r'material.setTint\(([\d.,]+),1.0\); material.setShadingModel\("unlit"\); '
                r"material.setReceiveLight\(false\);",
                replace,
                script,
            )
            if count == 0 or 'setShadingModel("unlit")' in script:
                raise ValueError(f"Unexpected generated material layout: {case}")
            script = script.replace(
                "road_camera.setActive(true);",
                "road_camera.setAutoExposure(false,-8.0,8.0);road_camera.setExposure(0.0);"
                "road_camera.setAmbient(0.08,0.08,0.08);road_camera.setActive(true);",
            ).replace(
                "gfx.setBackgroundColor(0.78,0.82,0.86,1.0);",
                "gfx.setBackgroundColor(0.0732389559,0.0732389559,0.0732389559,1.0);",
            )
            if view == "top":
                script = script.replace("setEye(32.0,18.0,32.0)", "setEye(0.0,64.0,0.0)")
                script = script.replace("setUp(0.0,1.0,0.0)", "setUp(0.0,0.0,-1.0)")
            (game / "main.nut").write_text(script, encoding="utf-8")
    print(f"Prepared {len(CASES)} native component scenes / {2 * len(CASES)} views")


def publish(args: argparse.Namespace) -> None:
    args.gallery.mkdir(parents=True, exist_ok=True)
    records = []
    cards = []
    overview = Image.new("RGB", (1440, 4 * 325), "#141b24")
    draw = ImageDraw.Draw(overview)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 17)
    for index, (case, label) in enumerate(CASES.items()):
        record = {"caseId": case, "label": label, "views": {}}
        pictures = []
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            frame = game / "frame.png"
            log = (game / "capture.log").read_text(encoding="utf-8", errors="replace")
            if "[capture] OK (engine frame)" not in log:
                raise ValueError(f"No engine capture proof: {case} {view}")
            with Image.open(frame) as image:
                if (
                    image.size != (1280, 720)
                    or image.convert("L").getextrema()[0] == image.convert("L").getextrema()[1]
                ):
                    raise ValueError(f"Invalid frame: {frame}")
                if view == "perspective":
                    thumb = image.convert("RGB")
                    thumb.thumbnail((472, 266))
                    x, y = (index % 3) * 480, (index // 3) * 325
                    overview.paste(thumb, (x, y + 28))
                    draw.text((x + 7, y + 5), case, fill="white", font=font)
            record["views"][view] = {
                "sha256": sha(frame),
                "inputs": {
                    p.name: sha(p)
                    for p in game.iterdir()
                    if p.suffix in (".nut", ".spv", ".jpg", ".png") and p != frame
                },
            }
            uri = "data:image/png;base64," + base64.b64encode(frame.read_bytes()).decode()
            pictures.append(
                f'<figure><figcaption>{view}</figcaption><img src="{uri}" loading="lazy"></figure>'
            )
        records.append(record)
        cards.append(
            f'<article id="{case}"><h2>{html.escape(label)} <small>{case}</small></h2><section>{"".join(pictures)}</section></article>'
        )
    overview.save(args.gallery / "components-overview.png")
    evidence = {
        "schema": "eve.tests.road-component-preview",
        "version": 1,
        "scope": "native full geometry only; no Unity component parity score",
        "material": "shared forward rough dielectric; asphalt texture plus solid category colors; no production asset fidelity claim",
        "runtimeSha256": sha(args.eve),
        "runtimeInventory": road_parity.renderer_runtime_hashes(args.eve),
        "sourceSnapshots": {str(p): sha(p) for p in (args.root / "source").rglob("*.json")},
        "generatorSha256": sha(args.generator),
        "cases": records,
    }
    (args.root / "evidence.json").write_text(
        json.dumps(evidence, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    page = (
        """<!doctype html><meta charset="utf-8"><title>道路可选部件实拍</title><style>
body{background:#141b24;color:#e5edf6;font:16px system-ui;margin:24px}p{max-width:1200px;line-height:1.7}a{color:#8fcbff}section{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%;cursor:zoom-in}small{font-size:14px;color:#9cacc0}article{margin:35px 0}dialog{background:#141b24;color:white;max-width:96vw}dialog img{max-width:92vw;width:auto}button{padding:12px}</style>
<h1>道路可选部件 · 11 场景 / 22 张真实帧</h1>
<p>EVEngine 当前道路生成器完整几何，透视与俯视并列。包含桥隧、护栏、步道、截面、prefab 变形、间距、接入口和地形。全部材质使用同一显式 forward 粗糙介电着色与 0 EV；沥青使用参考贴图，部件使用分类色材质，尚未替换成最终金属、混凝土和设施资源。没有开启阴影。</p>
<p>这是原生部件预览；Unity 的部件组合尚未同条件复拍，不计入路面 31/35 通过率。<a href="material-35.html">35 路面双端对照</a> · <a href="material-preview.html">之前的四场景修复前后</a>。点击图片查看原尺寸。</p>
"""
        + "".join(cards)
        + """<dialog id="zoom"><button onclick="zoom.close()">关闭</button><img id="large"></dialog><script>document.querySelectorAll('article img').forEach(i=>i.onclick=()=>{large.src=i.src;zoom.showModal()});</script>"""
    )
    (args.gallery / "components.html").write_text(page, encoding="utf-8")
    print("Published 11 native component scenes; 22 validated engine frames")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "publish"))
    parser.add_argument("--root", type=Path, default=Path("build/road-parity/component-preview"))
    parser.add_argument(
        "--generator",
        type=Path,
        default=Path("build/road-parity/native-verified/road_authoring_fixture.exe"),
    )
    parser.add_argument(
        "--eve", type=Path, default=Path("build/road-parity/host-profile/src/engine/eve.exe")
    )
    parser.add_argument("--project", type=Path, default=Path("build/road-parity/unity-project"))
    parser.add_argument("--material-root", type=Path, default=Path("build/road-parity/material-35"))
    parser.add_argument("--gallery", type=Path, default=Path("build/road-parity/gallery-35"))
    args = parser.parse_args()
    (prepare if args.command == "prepare" else publish)(args)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
