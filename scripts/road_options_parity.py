"""Independent optional-facility and grade-separated-road reference captures."""

from __future__ import annotations

import argparse
import html
import json
import math
import re
import shutil
from pathlib import Path

from PIL import Image, ImageDraw

import road_component_parity as comparison
import road_component_preview as preview
import road_junction_safety as safety
import road_parity
import utf8_stdio

CASES = {
    "option-median": "双车道与中央隔离带",
    "option-streetlights": "双侧路灯模块",
    "option-roadside-decor": "花箱、树形装饰与长椅",
    "option-boulevard": "隔离带、路灯、装饰组合",
    "interchange-grade4-angle30": "30° 斜交立交 / 4% 纵坡",
    "interchange-grade8-angle60": "60° 斜交立交 / 8% 纵坡",
    "interchange-grade12-angle90": "90° 正交立交 / 12% 纵坡",
    "interchange-banked-angle45": "45° 斜交立交 / 8% 纵坡 / 5° 横坡",
}
LIMITS = {
    "option-median": "两端隔离带与沥青的实际三角形足迹均无正面积重叠；两视角通过整图阈值。",
    "option-streetlights": "全部双侧灯具已生成；重复模块的首末位置与排列相位仍有差异。仅比较灯具网格，不包含夜景照明。",
    "option-roadside-decor": "两侧花箱、树形测试件与长椅均生成；重复模块端部与排列仍有差异。测试件使用分类色。",
    "option-boulevard": "隔离带、灯具与装饰共5条设施全部生成；局部排列与端部不同，组合场景未通过。",
}


def road_input(edge: dict) -> dict:
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
                "length": p["length"],
                "middle": comparison.source_mesh(p),
                "start": comparison.source_mesh(p.get("startMesh", {})),
                "end": comparison.source_mesh(p.get("endMesh", {})),
            }
        )
    return {
        "caseId": f"edge-{edge['id']}",
        "markers": [comparison.vector([p[0], p[1], -p[2]]) for p in edge["points"]],
        "shape": shape,
        "width": style["laneWidth"] * (edge["lanesForward"] + edge["lanesBackward"]),
        "closed": style["crossSectionClosed"],
        "facilities": facilities,
        "strengths": [p["splineStrength"] for p in edge["pointProfiles"]],
        "rolls": [p["rollDegrees"] for p in edge["pointProfiles"]],
        "widths": [p["widthScale"] for p in edge["pointProfiles"]],
    }


def author_checks(network: dict, case: str) -> dict:
    edges = network["edges"]
    if case.startswith("interchange-"):
        lower, upper = edges
        if {lower["from"], lower["to"]} & {upper["from"], upper["to"]}:
            raise ValueError("Grade separation must not connect at ground level")
        a, b = upper["points"]
        length = math.hypot(b[0] - a[0], b[2] - a[2])
        return {
            "gradePercent": 100 * (b[1] - a[1]) / length,
            "angleDegrees": math.degrees(math.atan2(b[2] - a[2], b[0] - a[0])),
            "rollDegrees": upper["pointProfiles"][0]["rollDegrees"],
            "centerDeckHeight": (a[1] + b[1]) / 2,
            "independentRoads": True,
        }
    if case in ("option-median", "option-boulevard"):
        centers = sorted(e["points"][0][2] for e in edges)
        if centers != [-4, 4]:
            raise ValueError("Expected separate carriageways and a 2m median gap")
        return {"medianGapMeters": 2, "medianWidthMeters": 1.2}
    return {"moduleLengthMeters": 8}


def native_road_triangles(source: Path) -> list:
    text = (source / "mesh_data.nut").read_text(encoding="utf-8")
    text = text.removeprefix("road_data <-").strip().removesuffix(";")
    text = re.sub(r"\b(positions|normals|uvs|indices)\s*=", r'"\1":', text)
    meshes = json.loads(re.sub(r",\s*([}\]])", r"\1", text))
    groups = json.loads((source / "groups.json").read_text(encoding="utf-8"))["groups"]
    triangles = []
    for group, mesh in zip(groups, meshes, strict=True):
        if group not in ("asphalt", "deck"):
            continue
        positions = mesh["positions"]
        for i in range(0, len(mesh["indices"]), 3):
            triangles.append(
                [tuple(positions[3 * k : 3 * k + 3]) for k in mesh["indices"][i : i + 3]]
            )
    return triangles


def heights_at(triangles: list, x: float, z: float) -> list[float]:
    heights = []
    for triangle in triangles:
        a, b, c = [(v[0], v[2]) for v in triangle]
        denominator = safety.cross(a, b, c)
        if abs(denominator) < 1e-9:
            continue
        weights = [
            safety.cross((x, z), b, c) / denominator,
            safety.cross(a, (x, z), c) / denominator,
            safety.cross(a, b, (x, z)) / denominator,
        ]
        if min(weights) >= -1e-6:
            heights.append(sum(w * v[1] for w, v in zip(weights, triangle, strict=True)))
    return heights


def measure_bridge(triangles: list, checks: dict) -> dict:
    center = heights_at(triangles, 0, 0)
    upper = [y for y in center if y > 4]
    lower = [y for y in center if y < 1]
    if not upper or not lower:
        raise ValueError("Missing actual separated deck at crossing")
    angle = math.radians(checks["angleDegrees"])
    endpoints = [
        max(heights_at(triangles, d * math.cos(angle), d * math.sin(angle))) for d in (-16, 16)
    ]
    clearance = min(upper) - max(lower)
    grade = 100 * (endpoints[1] - endpoints[0]) / 32
    if clearance < 8 or abs(grade - checks["gradePercent"]) > 0.1:
        raise ValueError(f"Incorrect actual bridge clearance/grade: {clearance}, {grade}")
    return {
        "centerClearanceMeters": clearance,
        "measuredGradePercent": grade,
        "centerTopMeters": max(upper),
        "centerBottomMeters": min(upper),
    }


def prepare(args: argparse.Namespace) -> None:
    preview.CASES = CASES
    preview.prepare(args)
    for case in CASES:
        source = args.root / "source" / case
        author = source / "recipe_network.json"
        network = json.loads(author.read_text(encoding="utf-8"))
        data = {
            "schema": "eve.tests.road-options-input",
            "version": 1,
            "caseId": case,
            "authorSha256": comparison.sha(author),
            "roads": [road_input(e) for e in network["edges"]],
            "authorChecks": author_checks(network, case),
        }
        (source / "unity-input.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
        groups = json.loads((source / "groups.json").read_text(encoding="utf-8"))["groups"]
        for view in ("perspective", "top"):
            game = args.root / "games" / f"{case}-{view}"
            lines = []
            for line in (game / "main.nut").read_text(encoding="utf-8").splitlines():
                match = re.match(r"\{ local data=road_data\[(\d+)\]", line)
                if match:
                    group = groups[int(match[1])]
                    if group.startswith("marking"):
                        continue
                    if group != "curb":
                        line = re.sub(
                            r"local albedo=[^;]+;", "local albedo=component_textures[0];", line
                        )
                        line = re.sub(
                            r"material.setNormalTexture\(component_textures\[\d+\]\)",
                            "material.setNormalTexture(component_textures[1])",
                            line,
                        )
                lines.append(
                    line.replace("setEye(32.0,18.0,32.0)", "setEye(48.0,36.0,48.0)").replace(
                        "setEye(0.0,64.0,0.0)", "setEye(0.0,92.0,0.0)"
                    )
                )
            (game / "main.nut").write_text("\n".join(lines) + "\n", encoding="utf-8")
    shutil.copyfile(
        "scripts/road_parity/UnityRoadOptionsCapture.cs",
        args.project / "Assets/Editor/UnityRoadOptionsCapture.cs",
    )
    print("Prepared 8 optional/grade-separated scenes, 16 native views")


def publish(args: argparse.Namespace) -> None:
    args.gallery.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "eve.tests.road-options-comparison",
        "version": 1,
        "referenceUseAudit": "provisional: lamps and decorations use procedural deformation rather than documented Mesh Object placement; bridge supports use a custom deformed source; image scores are diagnostic only",
        "runtime": road_parity.renderer_runtime_hashes(args.eve),
        "generatorSha256": comparison.sha(args.generator),
        "sourceSha256": {
            str(p): comparison.sha(p)
            for p in (
                Path(__file__),
                Path("scripts/road_parity/UnityRoadOptionsCapture.cs"),
                Path("scripts/road_parity/native/OptionalFixtures.cpp"),
                Path("scripts/road_parity/native/FunctionalFixtures.cpp"),
                Path("scripts/road_parity/material/contract.json"),
            )
        },
        "cases": [],
    }
    cards = []
    sheet = Image.new("RGB", (960, 8 * 155), "#141b24")
    draw = ImageDraw.Draw(sheet)
    for row, (case, label) in enumerate(CASES.items()):
        source = args.root / "source" / case
        evidence = json.loads((source / "unity-evidence.json").read_text(encoding="utf-8"))
        reopen = json.loads((source / "unity-reopen.json").read_text(encoding="utf-8"))
        if evidence.get("error") or not reopen.get("pixelsIdentical"):
            raise ValueError(f"SDK generation/reopen failed: {case}")
        if evidence["authorSha256"] != comparison.sha(source / "recipe_network.json"):
            raise ValueError("Author hash mismatch")
        if evidence["inputSha256"] != comparison.sha(source / "unity-input.json"):
            raise ValueError("Input hash mismatch")
        if evidence["adapterSha256"] != comparison.sha(
            Path("scripts/road_parity/UnityRoadOptionsCapture.cs")
        ):
            raise ValueError("Adapter changed after capture")
        if (
            evidence.get("schema") != "eve.tests.road-options-reference"
            or evidence.get("version") != 1
        ):
            raise ValueError("Unsupported SDK evidence schema")
        if evidence["footprintSha256"] != comparison.sha(source / "unity-footprint.json"):
            raise ValueError("Actual mesh snapshot hash mismatch")
        if reopen["adapterSha256"] != evidence["adapterSha256"]:
            raise ValueError("Saved-scene verification uses an older adapter")
        if reopen["sceneSha256"] != comparison.sha(args.project / evidence["scenePath"]):
            raise ValueError("Saved scene changed after verification")
        if reopen["frameSha256"] != comparison.sha(source / "unity-reopened-perspective.png"):
            raise ValueError("Reopened frame hash mismatch")
        record = {
            "id": case,
            "authorChecks": author_checks(
                json.loads((source / "recipe_network.json").read_text(encoding="utf-8")), case
            ),
            "unity": evidence,
            "reopen": reopen,
            "views": {},
        }
        expected_count = sum(
            len(r["facilities"])
            for r in json.loads((source / "unity-input.json").read_text(encoding="utf-8"))["roads"]
        )
        if len(evidence["generatedFacilities"]) != expected_count:
            raise ValueError("Missing individual optional facilities")
        if case.startswith("interchange-"):
            sdk = json.loads((source / "unity-footprint.json").read_text(encoding="utf-8"))
            sdk_triangles = [
                [(t[k]["x"], t[k]["y"], -t[k]["z"]) for k in ("a", "b", "c")] for t in sdk["road"]
            ]
            record["nativeBridge"] = measure_bridge(
                native_road_triangles(source), record["authorChecks"]
            )
            record["unityBridge"] = measure_bridge(sdk_triangles, record["authorChecks"])
        if case == "option-median":
            footprints = safety.native_footprint(source)
            record["nativeMedianFootprint"] = safety.audit(
                footprints["road"], footprints["facility"]
            )
            sdk = json.loads((source / "unity-footprint.json").read_text(encoding="utf-8"))

            def triangles(ts):
                return [[(t[k]["x"], t[k]["z"]) for k in ("a", "b", "c")] for t in ts]

            record["unityMedianFootprint"] = safety.audit(
                triangles(sdk["road"]), triangles(sdk["facility"])
            )
            if not all(
                record[k]["clear"] for k in ("nativeMedianFootprint", "unityMedianFootprint")
            ):
                raise ValueError("Median overlaps asphalt")
        parts = []
        for view in ("perspective", "top"):
            native = args.root / "games" / f"{case}-{view}" / "frame.png"
            unity = source / f"unity-{view}.png"
            if comparison.sha(unity) != evidence[view + "Sha256"]:
                raise ValueError("SDK frame changed after generation")
            game = native.parent
            if comparison.sha(game / "mesh_data.nut") != comparison.sha(source / "mesh_data.nut"):
                raise ValueError("Native rendered mesh differs from authored bake")
            if "[capture] OK" not in (game / "capture.log").read_text(encoding="utf-8"):
                raise ValueError("Missing engine-owned capture proof")
            diff = args.gallery / f"options-{case}-{view}-diff.png"
            metric = comparison.metrics(native, unity, diff)
            record["views"][view] = metric
            figures = []
            for title, image in (
                ("Unity / EasyRoads", unity),
                ("EVEngine", native),
                ("差值 ×4", diff),
            ):
                target = args.gallery / f"options-{case}-{view}-{title.split()[0]}.png"
                shutil.copyfile(image, target)
                figures.append(
                    f'<figure><figcaption>{html.escape(title)}</figcaption><a href="{target.name}" target="_blank"><img src="{target.name}"></a></figure>'
                )
            parts.append(
                f"<p>{view} · IoU {metric['surfaceIoU']:.4f} · MAE {metric['interiorRgbMAE']:.3f}/255 · {'通过' if metric['withinThresholds'] else '仍有差异'}</p><section>{''.join(figures)}</section>"
            )
            if view == "perspective":
                draw.text((8, row * 155 + 3), case, fill="white")
                for col, path in enumerate((unity, native, diff)):
                    thumb = Image.open(path).convert("RGB")
                    thumb.thumbnail((310, 130))
                    sheet.paste(thumb, (col * 320, row * 155 + 22))
        note = LIMITS.get(
            case,
            "桥面闭合截面、两侧护栏与支撑均实际生成；桥面端部、护栏偏移、桥墩随坡变形仍有差异，尚未通过。",
        )
        record["remainingDifference"] = note
        measured = ""
        if case.startswith("interchange-"):
            measured = f"实际中心净空：Unity {record['unityBridge']['centerClearanceMeters']:.3f}m / EVEngine {record['nativeBridge']['centerClearanceMeters']:.3f}m；实际纵坡 {record['unityBridge']['measuredGradePercent']:.2f}% / {record['nativeBridge']['measuredGradePercent']:.2f}%。"
        cards.append(
            f'<article id="{case}"><h2>{label}</h2><p>{html.escape(note)} {measured}</p><p>SDK 三角形 {evidence["triangles"]}，其中设施 {evidence["facilityTriangles"]}；设施种类 {len(evidence["generatedFacilities"])}。场景重新打开，透视帧逐字节一致。</p>{"".join(parts)}</article>'
        )
        report["cases"].append(record)
    (args.root / "comparison.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    sheet.save(args.gallery / "options-overview.png")
    page = """<!doctype html><meta charset="utf-8"><title>可选设施与坡度立交一致性对照</title><style>body{background:#141b24;color:#e4edf7;font:16px system-ui;margin:24px}section{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}figure{margin:0}img{width:100%}article{margin:40px 0}p{line-height:1.7}a{color:#8fcaff}</style><h1>可选设施与坡度立交 · 8 组 / 32 张双端真实帧</h1><p>每组独立生成 Unity/EasyRoads 与 EVEngine 网格，共用作者源 prefab、材质公式、纹理、分类色设施、光照、0 EV、相机。无阴影。路灯仅对照灯具几何，尚未验证夜景照明；装饰为测试形体。此处 prefab 使用沿路变形路径，未声明刚体直立放置一致。立交是独立分层道路、桥面、护栏及支撑的几何测试，尚非带匝道的完整互通。没有把 EVEngine 烘焙网格导入 Unity。</p><p>阈值沿用 IoU≥0.98、内部 MAE≤2/255、P95≤5/255。失败项如实保留；未并入原 35 场景通过率。<a href="junctions.html">路口对照</a> · <a href="components.html?view=consistency">部件对照</a> · <a href="material-35.html">35 路面</a></p>"""
    notice = '<aside style="border:2px solid #e8ad55;padding:16px;margin:20px 0"><strong>参考用法审计：本页暂不作为插件质量或功能对齐验收依据。</strong><p>路灯和装饰当前使用自制测试网格及 Procedural Mesh 变形路径，尚未按随包手册的 Mesh Object 独立实例方式建立参考；桥墩也是自制整体变形源。金色分类材质、无阴影画面不代表插件成品效果。以下数值仅描述本批测试夹具，不能据此判定 EasyRoads 质量或将差异全部归因于 EVEngine。参考用法校正后需重新拍摄和评分。</p></aside>'
    (args.gallery / "options.html").write_text(
        page.replace("</h1>", "</h1>" + notice, 1) + "".join(cards), encoding="utf-8"
    )
    print("Published 8 cases / 32 frames; independent references and saved-scene verification")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "publish"))
    for name, default in {
        "root": "build/road-parity/options-parity",
        "generator": "build/road-parity/native-verified/road_authoring_fixture.exe",
        "eve": "build/road-parity/host-profile/src/engine/eve.exe",
        "project": "build/road-parity/unity-project",
        "material-root": "build/road-parity/material-35",
        "gallery": "build/road-parity/gallery-35",
    }.items():
        parser.add_argument("--" + name, type=Path, default=Path(default))
    args = parser.parse_args()
    {"prepare": prepare, "publish": publish}[args.command](args)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
