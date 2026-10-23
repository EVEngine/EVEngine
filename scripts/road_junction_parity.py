"""Create independent Unity junction reference scenes and native diagnostic comparisons."""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import road_component_parity as comparison
import road_component_preview as preview
import road_junction_safety as safety
import utf8_stdio

CASES = {
    "junction-tee": "T 型路口",
    "junction-cross": "十字交叉口",
    "junction-roundabout": "四接入环岛",
    "tee-facilities": "带连续护栏的 T 型路口",
    "multi-arm-facilities": "四臂路口与连续设施",
}


def prepare(args: argparse.Namespace) -> None:
    # Reuse the existing material renderer preparation; this process owns its case catalogue.
    preview.CASES = CASES
    preview.prepare(args)
    for case in CASES:
        directory = args.root / "source" / case
        author = directory / "recipe_network.json"
        network = json.loads(author.read_text(encoding="utf-8"))
        nodes = {n["id"]: n for n in network["nodes"]}
        degrees = Counter(i for e in network["edges"] for i in (e["from"], e["to"]))
        roundabout = case == "junction-roundabout"
        hub_id = max(degrees, key=degrees.get)
        hub = [0, 0, 0] if roundabout else [nodes[hub_id][k] for k in ("x", "y", "z")]
        edges = network["edges"]
        if roundabout:
            edges = [e for e in edges if degrees[e["from"]] == 1 or degrees[e["to"]] == 1]
        roads = []
        for edge in edges:
            points = edge["points"]
            at_start = degrees[edge["from"]] != 1 if roundabout else edge["from"] == hub_id
            roads.append(
                {
                    "id": edge["id"],
                    "sidewalks": not bool(edge["style"]["crossSection"]),
                    **{
                        k: edge["style"][k]
                        for k in ("curbWidth", "curbHeight", "sidewalkWidth", "sidewalkHeight")
                    },
                    "hubAtStart": at_start,
                    "width": edge["style"]["laneWidth"]
                    * (edge["lanesForward"] + edge["lanesBackward"]),
                    "markers": [comparison.vector([p[0], p[1], -p[2]]) for p in points],
                    "strengths": [p["splineStrength"] for p in edge["pointProfiles"]],
                    "rolls": [p["rollDegrees"] for p in edge["pointProfiles"]],
                    "widths": [p["widthScale"] for p in edge["pointProfiles"]],
                }
            )
        facility = edges[0]["style"]["facilities"]
        profile = []
        if facility:
            profile = [{"x": -p["side"], "y": p["up"]} for p in facility[0]["profile"]]
            profile.append(profile[0])
        limit = (
            "环岛参考已按作者12m中心半径、3.25m环宽重建并更新四端口；原生为八段环形图，接入口圆角及步道衔接仍有差异。"
            if roundabout
            else "参考使用默认 T/X prefab；原生由图烘焙。沿道路的路缘/步道使用作者截面和SDK真实生成；连接内部转角步道尚待对应。"
        )
        if facility:
            limit += " 参考为作者指定路由建立独立SDK隐藏承载道路，仅生成中间转向设施；未将此等同于包内路口自动衔接。"
        data = {
            "schema": "eve.tests.road-junction-input",
            "version": 2,
            "caseId": case,
            "authorSha256": comparison.sha(author),
            "prefab": "Roundabout 4 connections"
            if roundabout
            else "Default T Crossing"
            if len(edges) == 3
            else "Default X Crossing",
            "hub": comparison.vector([hub[0], hub[1], -hub[2]]),
            "roads": roads,
            "ringRadius": 12 if roundabout else 0,
            "ringWidth": network["edges"][0]["style"]["laneWidth"] if roundabout else 0,
            "routes": nodes[hub_id]["connection"]["facilityRoutes"] if not roundabout else [],
            "facilities": bool(facility),
            "facilityProfile": profile,
            "facilitySide": -facility[0]["lateralOffset"] if facility else 0,
            "facilityUp": facility[0]["verticalOffset"] if facility else 0,
            "mappingLimit": limit,
        }
        (directory / "unity-input.json").write_text(
            json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        groups = json.loads((directory / "groups.json").read_text(encoding="utf-8"))
        if (
            groups.get("schema") != "eve.tests.road-component-groups"
            or groups.get("schemaVersion") != 1
        ):
            raise ValueError("Unsupported group fixture")
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            lines = []
            for line in (game / "main.nut").read_text(encoding="utf-8").splitlines():
                match = re.match(r"\{ local data=road_data\[(\d+)\]", line)
                if match:
                    group = groups["groups"][int(match[1])]
                    if group.startswith("marking"):
                        continue
                    if not (facility and group == "curb"):
                        line = re.sub(
                            r"local albedo=[^;]+;", "local albedo=component_textures[0];", line
                        )
                        line = re.sub(
                            r"material.setNormalTexture\(component_textures\[\d+\]\)",
                            "material.setNormalTexture(component_textures[1])",
                            line,
                        )
                line = line.replace("setEye(32.0,18.0,32.0)", "setEye(48.0,36.0,48.0)")
                line = line.replace("setEye(0.0,64.0,0.0)", "setEye(0.0,92.0,0.0)")
                lines.append(line)
            (game / "main.nut").write_text("\n".join(lines) + "\n", encoding="utf-8")
    shutil.copyfile(
        "scripts/road_parity/UnityJunctionCapture.cs",
        args.project / "Assets/Editor/UnityJunctionCapture.cs",
    )
    shutil.copyfile(
        "scripts/road_parity/UnityJunctionAuthoring.cs",
        args.project / "Assets/Editor/UnityJunctionAuthoring.cs",
    )
    print("Prepared 5 junctions / 10 native views; independent SDK prefab references")


def validate_generation(author: dict, evidence: dict) -> None:
    if author.get("schema") != "eve.tests.road-junction-input" or author.get("version") != 2:
        raise ValueError("Unsupported author input")
    if evidence.get("generatedWalks") != 2 * sum(bool(r["sidewalks"]) for r in author["roads"]):
        raise ValueError("Missing generated sidewalks")
    if evidence.get("explicitRoutes") != len(author["routes"]):
        raise ValueError("Missing explicit facility routes")
    if any(
        abs(p["requestedWidth"] - p["actualWidth"]) > 0.001 for p in evidence.get("attachments", [])
    ):
        raise ValueError("SDK road width differs from author width")
    if author["ringRadius"] and (
        abs(evidence.get("measuredRingRadius", 0) - author["ringRadius"]) > 0.001
        or abs(evidence.get("measuredRingWidth", 0) - author["ringWidth"]) > 0.001
    ):
        raise ValueError("Ring dimensions were not generated")


def publish(args: argparse.Namespace) -> None:
    log = args.unity_log.read_text(encoding="utf-8", errors="replace")
    if "JUNCTION_REFERENCE_COMPLETE" not in log or "returncode=0" not in log:
        raise ValueError("Missing successful Unity process completion")
    reopen_log = args.reopen_log.read_text(encoding="utf-8", errors="replace")
    if "JUNCTION_REOPEN_COMPLETE" not in reopen_log or "returncode=0" not in reopen_log:
        raise ValueError("Missing successful saved-scene verification")
    report = {
        "schema": "eve.tests.road-junction-comparison",
        "version": 1,
        "scope": "Uniform-material junction geometry/shading diagnostic; not full material or facility-route acceptance",
        "cases": [],
        "runtime": preview.road_parity.renderer_runtime_hashes(args.eve),
        "unityLogSha256": comparison.sha(args.unity_log),
        "reopenLogSha256": comparison.sha(args.reopen_log),
        "sourceSha256": {
            str(p): comparison.sha(p)
            for p in (
                Path("scripts/road_parity/UnityJunctionCapture.cs"),
                Path("scripts/road_parity/UnityJunctionAuthoring.cs"),
                Path("scripts/road_junction_safety.py"),
                Path("src/modules/procgen/road/RoadBakeJunctions.cpp"),
                Path("src/modules/procgen/road/RoadAlgorithmRevision.h"),
                Path("test/procgen_road_junction_style.cpp"),
                Path("scripts/road_parity/native/FunctionalFixtures.cpp"),
                Path("scripts/road_parity/material/RoadAsphaltCommon.inc"),
                Path("scripts/road_parity/material/contract.json"),
            )
        },
        "files": {},
    }
    cards = []
    sheet = Image.new("RGB", (1440, 45 + len(CASES) * 320), "#141b24")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 17)
    draw.text(
        (8, 8),
        "Junction references: Unity / EasyRoads | EVEngine | RGB difference x4",
        font=font,
        fill="white",
    )
    for index, (case, label) in enumerate(CASES.items()):
        directory = args.root / "source" / case
        evidence = json.loads((directory / "unity-evidence.json").read_text(encoding="utf-8"))
        if (
            evidence.get("schema") != "eve.tests.road-junction-reference"
            or evidence.get("version") != 2
        ):
            raise ValueError("Unsupported reference evidence schema")
        validate_generation(
            json.loads((directory / "unity-input.json").read_text(encoding="utf-8")), evidence
        )
        if evidence.get("error"):
            raise ValueError(evidence["error"])
        expected = {
            "inputSha256": directory / "unity-input.json",
            "authorSha256": directory / "recipe_network.json",
            "adapterSha256": Path("scripts/road_parity/UnityJunctionCapture.cs"),
            "authoringAdapterSha256": Path("scripts/road_parity/UnityJunctionAuthoring.cs"),
            "referenceAssemblySha256": args.project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll",
        }
        for key, path in expected.items():
            if evidence[key] != comparison.sha(path):
                raise ValueError(f"Stale {key}: {case}")
        if evidence["generatedConnections"] != 1 or len(
            {p["portIndex"] for p in evidence["attachments"]}
        ) != len(evidence["attachments"]):
            raise ValueError("Invalid junction connection/port ownership")
        if len(evidence["attachments"]) not in (3, 4) or any(
            p["endpointError"] > 0.001 for p in evidence["attachments"]
        ):
            raise ValueError("Incomplete or detached junction reference")
        scene = args.project / evidence["scenePath"]
        if not scene.exists():
            raise ValueError("Missing saved reference scene")
        reopened = json.loads((directory / "unity-reopen.json").read_text(encoding="utf-8"))
        if (
            reopened.get("schema") != "eve.tests.road-junction-scene-reopen"
            or reopened.get("version") != 1
            or not reopened.get("pixelsIdentical")
            or reopened["sceneSha256"] != comparison.sha(scene)
            or reopened["adapterSha256"] != evidence["adapterSha256"]
            or reopened["frameSha256"] != evidence["perspectiveSha256"]
            or reopened["triangles"] != evidence["triangles"]
            or comparison.sha(directory / "unity-reopened-perspective.png")
            != evidence["perspectiveSha256"]
        ):
            raise ValueError(f"Unverified reopened scene: {case}")
        row = {
            "caseId": case,
            "label": label,
            "reference": evidence,
            "sceneSha256": comparison.sha(scene),
            "reopenedFrameIdentical": True,
            "views": [],
        }
        parts = []
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            native, unity = game / "frame.png", directory / f"unity-{view}.png"
            if "[capture] OK (engine frame)" not in (game / "capture.log").read_text(
                encoding="utf-8", errors="replace"
            ):
                raise ValueError("Missing native frame capture")
            if comparison.sha(game / "mesh_data.nut") != comparison.sha(
                directory / "mesh_data.nut"
            ):
                raise ValueError("Stale native mesh")
            if comparison.sha(unity) != evidence[f"{view}Sha256"]:
                raise ValueError("Reference frame changed after capture")
            diff = directory / f"difference-{view}.png"
            metrics = comparison.metrics(native, unity, diff)
            metrics["view"] = view
            row["views"].append(metrics)
            parts.append(
                f"<p>{view} · IoU {metrics['surfaceIoU']:.5f} · RGB MAE {metrics['interiorRgbMAE']:.3f}/255</p><section>"
                + comparison.picture(unity, "Unity / EasyRoads")
                + comparison.picture(native, "EVEngine")
                + comparison.picture(diff, "RGB 绝对差 ×4")
                + "</section>"
            )
            if view == "perspective":
                y = 45 + index * 320
                draw.text(
                    (8, y), f"{case} | IoU {metrics['surfaceIoU']:.5f}", font=font, fill="white"
                )
                for column, path in enumerate((unity, native, diff)):
                    image = Image.open(path).convert("RGB")
                    image.thumbnail((475, 268))
                    sheet.paste(image, (column * 480, y + 28))
            for path in game.iterdir():
                if path.is_file() and path.name != "capture.log":
                    report["files"][str(path)] = comparison.sha(path)
        safety_text = ""
        if case in ("tee-facilities", "multi-arm-facilities"):
            if evidence.get("footprintSha256") != comparison.sha(
                directory / "unity-footprint.json"
            ):
                raise ValueError("Stale or missing Unity guardrail footprint")
            row["guardrailSafety"] = safety.validate_case(directory)
            if any(not value["clear"] for value in row["guardrailSafety"].values()):
                raise ValueError(f"Guardrail enters driving surface: {case}")
            safety_text = "<p>外缘护栏网格检查：Unity 与 EVEngine 均无沥青面正面积交叠（逐三角形 XZ 足迹检查，非整图相似度判断）。</p>"
        row["withinImageThresholds"] = all(v["withinThresholds"] for v in row["views"])
        report["cases"].append(row)
        cards.append(
            f'<article id="{case}"><h2>{html.escape(label)}</h2><p>真实 SDK 连接数：{evidence["generatedConnections"]}；已验证接入：{len(evidence["attachments"])}。Unity 场景：{html.escape(evidence["scenePath"])}</p><p>{html.escape(evidence["mappingLimit"])}</p>{safety_text}{"".join(parts)}</article>'
        )
    for path in (args.project / "Assets/RoadJunctionParity").rglob("*"):
        if path.is_file():
            report["files"][str(path)] = comparison.sha(path)
    (args.root / "comparison.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    sheet.save(args.gallery / "junction-overview.png")
    page = """<!doctype html><meta charset="utf-8"><title>路口一致性参考</title><style>body{background:#141b24;color:#e4edf7;font:16px system-ui;margin:24px}p{line-height:1.65}section{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}figure{margin:0}img{width:100%;cursor:zoom-in}article{margin:45px 0}a{color:#8fcaff}dialog{background:#141b24;color:white;max-width:96vw}dialog img{width:auto;max-width:92vw}</style><h1>Unity 路口 / 交叉口参考 · 双端真实帧</h1><p>5 组场景，各有透视和俯视，共 20 帧。Unity 使用包内真实 T/X/环岛连接 prefab 与公开道路接入接口；EVEngine 独立图烘焙。两端共用材质公式、纹理、光照、0 EV、相机和背景。此为统一材质的几何与着色诊断：去掉原生几何标线，路缘/步道也用同一沥青材质，设施用相同分类色。不是最终生产材质对照，也不代表设施路由已经一致。</p><p>阈值保留 IoU≥0.98、内部 RGB MAE≤2/255、P95≤5/255。每个场景都保存最终端口与首末点距离、网格统计、来源哈希和可重新打开的 Unity 场景；五个场景均已在另一个 Unity 进程重新打开，重拍帧逐字节一致。环岛尺寸已经实测重建；人行道转角与设施自动衔接边界保留在各项说明中。<a href="components.html">部件对照</a> · <a href="material-35.html">35 路面对照</a></p>"""
    page += (
        "".join(cards)
        + '<dialog id="zoom"><button onclick="zoom.close()">关闭</button><img id="large"></dialog><script>document.querySelectorAll("article img").forEach(i=>i.onclick=()=>{large.src=i.src;zoom.showModal()});</script>'
    )
    (args.gallery / "junctions.html").write_text(page, encoding="utf-8")
    print("Published 5 junction reference scenes / 20 verified frames")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "publish"))
    for name, default in {
        "root": "build/road-parity/junction-parity",
        "generator": "build/road-parity/native-verified/road_authoring_fixture.exe",
        "eve": "build/road-parity/host-profile/src/engine/eve.exe",
        "project": "build/road-parity/unity-project",
        "material-root": "build/road-parity/material-35",
        "gallery": "build/road-parity/gallery-35",
        "unity-log": "build/road-parity/junction-parity-unity.log",
        "reopen-log": "build/road-parity/junction-parity-reopen.log",
    }.items():
        parser.add_argument("--" + name, type=Path, default=Path(default))
    args = parser.parse_args()
    {"prepare": prepare, "publish": publish}[args.command](args)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
