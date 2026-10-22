"""Independent EasyRoads/native optional-component comparisons with explicit adapter gaps."""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import re
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

import road_component_preview as preview
import utf8_stdio

GAPS = {
    "bridge-tunnel": "参考适配尚未覆盖桥墩规则和桥隧区间；缺参考帧，不能判定一致",
    "custom-connection": "自定义接入口及端口原子插入缺独立参考适配",
    "multi-arm-facilities": "已补建真实 Unity 四臂路口及设施参考，见路口对照页；指定设施路由对仍未完整映射，本页原完整组合暂不评分。",
    "terrain-multichannel": "地形多通道写回尚未接入此组合的双端渲染适配",
    "tunnel-portals": "入口开孔与隧道覆盖地形尚未接入此组合的双端渲染适配",
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def vector(values: list[float]) -> dict:
    return dict(zip("xyz", values, strict=False))


def source_mesh(prefab: dict) -> dict:
    mesh = prefab.get("mesh", {})
    vertices = mesh.get("vertices", [])
    return {
        "positions": [vector([-v[0], v[1], v[2]]) for v in vertices],
        "normals": [vector([-v[3], v[4], v[5]]) for v in vertices],
        "uvs": [{"x": v[6], "y": v[7]} for v in vertices],
        "indices": [i for t in mesh.get("triangles", []) for i in (t[0], t[2], t[1])],
    }


def prepare(args: argparse.Namespace) -> None:
    preview.prepare(args)
    for case in preview.CASES:
        directory = args.root / "source" / case
        author = directory / "recipe_network.json"
        if case in GAPS:
            continue
        network = json.loads(author.read_text(encoding="utf-8"))
        edge = network["edges"][0]
        style = edge["style"]
        shape = [{"x": -p["side"], "y": p["up"]} for p in style["crossSection"]]
        if style["crossSectionClosed"]:
            shape.append(shape[0])
        facilities = []
        for f in style["facilities"]:
            profile = [{"x": -p["side"], "y": p["up"]} for p in f["profile"]]
            profile.append(profile[0])
            p = f["prefab"]
            facilities.append(
                {
                    "profile": profile,
                    "x": -f["lateralOffset"],
                    "y": f["verticalOffset"],
                    "capStart": f["capStart"],
                    "capEnd": f["capEnd"],
                    "automatic": p["automaticSpacing"],
                    "middle": source_mesh(p),
                    "start": source_mesh(p.get("startMesh", {})),
                    "end": source_mesh(p.get("endMesh", {})),
                }
            )
        data = {
            "caseId": case,
            "authorSha256": sha(author),
            "markers": [vector([p[0], p[1], -p[2]]) for p in edge["points"]],
            "shape": shape,
            "width": style["laneWidth"] * (edge["lanesForward"] + edge["lanesBackward"]),
            "closed": style["crossSectionClosed"],
            "facilities": facilities,
            "strengths": [p["splineStrength"] for p in edge["pointProfiles"]],
            "rolls": [p["rollDegrees"] for p in edge["pointProfiles"]],
            "widths": [p["widthScale"] for p in edge["pointProfiles"]],
        }
        (directory / "unity-input.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    shutil.copyfile(
        "scripts/road_parity/UnityComponentCapture.cs",
        args.project / "Assets/Editor/UnityComponentCapture.cs",
    )


def metrics(native_path: Path, unity_path: Path, diff_path: Path) -> dict:
    arrays = []
    for path in (native_path, unity_path):
        image = Image.open(path).convert("RGB")
        if image.size != (1280, 720):
            raise ValueError(f"Wrong frame size: {path}")
        arrays.append(np.asarray(image, dtype=np.int16))
    native, unity = arrays
    masks = [np.max(np.abs(a - a[0, 0]), axis=2) > 2 for a in arrays]
    union, common = masks[0] | masks[1], masks[0] & masks[1]
    if not union.any() or not masks[0].any() or not masks[1].any():
        raise ValueError("Empty captured geometry")
    core = (
        np.asarray(Image.fromarray((common * 255).astype("uint8")).filter(ImageFilter.MinFilter(5)))
        > 0
    )
    difference = np.abs(native - unity)
    Image.fromarray(np.clip(difference * 4, 0, 255).astype("uint8")).save(diff_path)
    mae = float(difference[core].mean()) if core.any() else None
    p95 = float(np.percentile(difference[core], 95)) if core.any() else None
    iou = float(common.sum() / union.sum())
    return {
        "surfaceIoU": iou,
        "interiorRgbMAE": mae,
        "interiorRgbP95": p95,
        "interiorPixels": int(core.sum()),
        "withinThresholds": iou >= 0.98 and mae is not None and mae <= 2 and p95 <= 5,
        "nativeSha256": sha(native_path),
        "unitySha256": sha(unity_path),
    }


def diagnostics(args: argparse.Namespace) -> None:
    for case in (case for case in preview.CASES if case not in GAPS):
        groups = json.loads(
            (args.root / "source" / case / "groups.json").read_text(encoding="utf-8")
        )
        if (
            groups.get("schema") != "eve.tests.road-component-groups"
            or groups.get("schemaVersion") != 1
        ):
            raise ValueError("Unsupported component group evidence")
        groups = groups["groups"]
        facility = case not in ("crowned-section", "concave-beam")
        for view in ("perspective", "top"):
            source = args.root / "games" / f"{case}-{view}"
            target = args.root / "diagnostics" / f"{case}-{view}"
            target.mkdir(parents=True, exist_ok=True)
            for path in source.iterdir():
                if path.suffix in (".nut", ".spv", ".jpg", ".png") and path.name != "frame.png":
                    shutil.copyfile(path, target / path.name)
            script = (source / "main.nut").read_text(encoding="utf-8")
            lines = []
            for line in script.splitlines():
                match = re.match(r"\{ local data=road_data\[(\d+)\]", line)
                if match:
                    group = groups[int(match[1])]
                    if (facility and group != "curb") or (
                        not facility and group.startswith("marking")
                    ):
                        continue
                    if not facility:
                        line = re.sub(
                            r"local albedo=[^;]+;", "local albedo=component_textures[0];", line
                        )
                        line = re.sub(
                            r"material.setNormalTexture\(component_textures\[\d+\]\)",
                            "material.setNormalTexture(component_textures[1])",
                            line,
                        )
                lines.append(line)
            (target / "main.nut").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("Prepared 12 isolated native diagnostic captures")


def publish(args: argparse.Namespace) -> None:
    log = args.unity_log.read_text(encoding="utf-8", errors="replace")
    if "COMPONENT_REFERENCE_COMPLETE" not in log or "returncode=0" not in log:
        raise ValueError("Unity completion and process exit evidence required")
    rows = []
    report = {
        "schema": "eve.tests.road-component-comparison",
        "version": 1,
        "scope": "full-scene optional-component comparisons; mapping limitations shown per case",
        "nativeSourceAndInputs": {
            str(p): sha(p)
            for folder in (args.root / "games", args.root / "diagnostics", args.root / "source")
            for p in folder.rglob("*")
            if p.is_file()
            and p.suffix in (".nut", ".spv", ".jpg", ".json")
            and not p.name.startswith("unity-")
        },
        "thresholds": {"IoU": 0.98, "RGB_MAE": 2, "RGB_P95": 5},
        "cases": [],
        "runtime": preview.road_parity.renderer_runtime_hashes(args.eve),
        "adapterSha256": sha(Path("scripts/road_parity/UnityComponentCapture.cs")),
        "unityLogSha256": sha(args.unity_log),
    }
    for case, label in preview.CASES.items():
        directory = args.root / "source" / case
        row = {
            "caseId": case,
            "label": label,
            "views": [],
            "status": "missing-reference",
            "gap": GAPS.get(case),
        }
        evidence_path = directory / "unity-evidence.json"
        if case not in GAPS and evidence_path.exists():
            evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
            if evidence["inputSha256"] != sha(directory / "unity-input.json") or evidence[
                "authorSha256"
            ] != sha(directory / "recipe_network.json"):
                raise ValueError(f"Stale inputs: {case}")
            if evidence["adapterSha256"] != report["adapterSha256"]:
                raise ValueError(f"Stale reference adapter: {case}")
            if evidence["referenceSha256"] != sha(
                args.project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"
            ):
                raise ValueError(f"Stale reference assembly: {case}")
            report.setdefault("referenceAssemblySha256", evidence["referenceSha256"])
            if evidence.get("error"):
                row["gap"] = "参考生成失败：" + evidence["error"]
            else:
                row["status"] = "compared"
                row["referenceEvidence"] = evidence
        cards = []
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            if "[capture] OK (engine frame)" not in (game / "capture.log").read_text(
                encoding="utf-8", errors="replace"
            ):
                raise ValueError(f"No native frame evidence: {case} {view}")
            native = game / "frame.png"
            if sha(game / "mesh_data.nut") != sha(directory / "mesh_data.nut"):
                raise ValueError(f"Stale native fixture mesh: {case}")
            if row["status"] == "compared":
                unity = directory / f"unity-{view}.png"
                diff = directory / f"difference-{view}.png"
                m = metrics(native, unity, diff)
                m["view"] = view
                row["views"].append(m)
                cards.append(
                    f"<p>{view} · IoU {m['surfaceIoU']:.5f} · RGB MAE {m['interiorRgbMAE']}</p>"
                )
                cards.append(
                    "<section>"
                    + picture(unity, "Unity / EasyRoads")
                    + picture(native, "EVEngine")
                    + picture(diff, "RGB 差 ×4")
                    + "</section>"
                )
                diagnostic = args.root / "diagnostics" / f"{case}-{view}"
                if (diagnostic / "frame.png").exists():
                    if "[capture] OK (engine frame)" not in (diagnostic / "capture.log").read_text(
                        encoding="utf-8", errors="replace"
                    ):
                        raise ValueError("Missing diagnostic capture evidence")
                    reference = (
                        directory
                        / f"unity-{'facility-' if case not in ('crowned-section', 'concave-beam') else ''}{view}.png"
                    )
                    difference = directory / f"diagnostic-difference-{view}.png"
                    d = metrics(diagnostic / "frame.png", reference, difference)
                    d["view"] = view
                    row.setdefault("diagnostics", []).append(d)
                    cards.append(
                        f"<p>隔离对照：{'仅围栏' if case not in ('crowned-section', 'concave-beam') else '截面统一沥青材质、去掉几何标线'} · IoU {d['surfaceIoU']:.5f} · RGB MAE {format(d['interiorRgbMAE'], '.3f') if d['interiorRgbMAE'] is not None else 'N/A（共同区域过薄）'}/255</p>"
                    )
                    cards.append(
                        "<section>"
                        + picture(reference, "Unity / EasyRoads")
                        + picture(diagnostic / "frame.png", "EVEngine")
                        + picture(difference, "隔离 RGB 差 ×4")
                        + "</section>"
                    )
            else:
                cards.append(
                    '<section><div class="missing">Unity：缺少可比较参考，不能评分</div>'
                    + picture(native, "EVEngine / " + view)
                    + "</section>"
                )
        if row["status"] == "compared":
            row["status"] = (
                "passed" if all(m["withinThresholds"] for m in row["views"]) else "different"
            )
            row["gap"] = (
                "完整场景的标线及材质分组尚未完全投影。请结合隔离对照判断：截面两端使用相同沥青材质；围栏两端使用相同分类色材质。prefab 固定重复数、端盖、平滑组与自动间距的参考配置仍待校准，当前差异包含适配层因素。"
            )
        row["inputProjectionComplete"] = False
        report["cases"].append(row)
        rows.append(
            f"<article><h2>{html.escape(label)} <small>{case} / {row['status']}</small></h2><p>{html.escape(row['gap'] or '')}</p>{''.join(cards)}</article>"
        )
    report["comparedCases"] = sum(c["status"] in ("passed", "different") for c in report["cases"])
    report["passedCases"] = sum(c["status"] == "passed" for c in report["cases"])
    (args.root / "comparison.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    page = """<!doctype html><meta charset="utf-8"><title>可选部件一致性对照</title><style>body{background:#141b24;color:#e1eaf5;font:16px system-ui;margin:24px}p{line-height:1.6;white-space:pre-wrap;overflow-wrap:anywhere}section{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}figure{margin:0}img{width:100%;cursor:zoom-in}article{margin:40px 0}small{font-size:14px;color:#aabbd0}.missing{padding:35px;background:#352831}dialog{background:#141b24;color:white;max-width:96vw}dialog img{width:auto;max-width:92vw}</style><h1>可选部件 · Unity / EVEngine 一致性对照</h1>"""
    page += '<p><a href="junctions.html">Unity T 型、十字、环岛及带设施路口一致性参考</a> · <a href="options.html">新增：隔离带、路灯、路边装饰与坡度立交对照</a></p>'
    page += f"<p>11 项清单：{report['comparedCases']} 项有实际双端帧，{11 - report['comparedCases']} 项缺完整参考适配。两端按同一作者数据独立生成，共用着色公式、光照和相机。完整场景的材质分组与标线映射尚有缺口，所以另附相同材质的隔离对照；prefab 的固定重复数、端盖和自动间距映射仍待校准。截图差异不能全部归因于引擎。不能继承路面 31/35 的分数。既定阈值：IoU≥0.98，内部 RGB MAE≤2/255，P95≤5/255。</p>"
    page += (
        "".join(rows)
        + '<dialog id="zoom"><button onclick="zoom.close()">关闭</button><img id="large"></dialog><script>document.querySelectorAll("article img").forEach(x=>x.onclick=()=>{large.src=x.src;zoom.showModal()});</script>'
    )
    (args.gallery / "component-comparison.html").write_text(page, encoding="utf-8")
    (args.gallery / "components.html").write_text(page, encoding="utf-8")
    print(
        f"Compared {report['comparedCases']}/11; passed {report['passedCases']}; gaps explicitly retained"
    )


def picture(path: Path, label: str) -> str:
    uri = "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode()
    return f'<figure><figcaption>{html.escape(label)}</figcaption><img loading="lazy" src="{uri}"></figure>'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "diagnostics", "publish"))
    for name, default in {
        "root": "build/road-parity/component-parity",
        "generator": "build/road-parity/native-verified/road_authoring_fixture.exe",
        "eve": "build/road-parity/host-profile/src/engine/eve.exe",
        "project": "build/road-parity/unity-project",
        "material-root": "build/road-parity/material-35",
        "gallery": "build/road-parity/gallery-35",
        "unity-log": "build/road-parity/component-parity-unity.log",
    }.items():
        parser.add_argument("--" + name, type=Path, default=Path(default))
    args = parser.parse_args()
    {"prepare": prepare, "diagnostics": diagnostics, "publish": publish}[args.command](args)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
