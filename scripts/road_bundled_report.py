"""Publish original-example correspondence with explicit calibration and generation scopes."""

from __future__ import annotations

import argparse
import html
import json
import shutil
import tarfile
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

import road_parity
from road_bundled_alignment import json_read, sha, shader_ambient
import utf8_stdio


def frame(path: Path) -> np.ndarray:
    image = Image.open(path).convert("RGB")
    if image.size != (1280, 720):
        raise ValueError(f"Wrong captured dimensions: {path}")
    return np.asarray(image, dtype=np.int16)


def compare(
    reference: Path, candidate: Path, ref_mask: Path, candidate_mask: Path
) -> tuple[dict, Image.Image]:
    ref, ev = frame(reference), frame(candidate)
    a, b = frame(ref_mask)[:, :, 0] >= 127, frame(candidate_mask)[:, :, 0] >= 127
    if not a.any() or not b.any():
        raise ValueError("Empty road evidence mask")
    intersection, union = a & b, a | b
    core = (
        np.asarray(
            Image.fromarray((intersection * 255).astype("uint8")).filter(ImageFilter.MinFilter(5))
        )
        > 0
    )
    ad = np.asarray(Image.fromarray((a * 255).astype("uint8")).filter(ImageFilter.MaxFilter(3))) > 0
    bd = np.asarray(Image.fromarray((b * 255).astype("uint8")).filter(ImageFilter.MaxFilter(3))) > 0
    delta = np.abs(ref - ev)
    metric = {
        "roadMaskIoU": float(intersection.sum() / union.sum()),
        "fullRgbMAE": float(delta.mean()),
        "roadInteriorRgbMAE": float(delta[core].mean()) if core.any() else None,
        "roadInteriorRgbP95": float(np.percentile(delta[core], 95)) if core.any() else None,
        "roadInteriorPixels": int(core.sum()),
        "maskPixelsBeyondOnePixelBand": int((a & ~bd).sum() + (b & ~ad).sum()),
        "referenceSha256": sha(reference),
        "candidateSha256": sha(candidate),
        "referenceMaskSha256": sha(ref_mask),
        "candidateMaskSha256": sha(candidate_mask),
    }
    metric["imageThresholdsMet"] = (
        metric["roadMaskIoU"] >= 0.98
        and metric["roadInteriorRgbMAE"] is not None
        and metric["roadInteriorRgbMAE"] <= 2
        and metric["roadInteriorRgbP95"] <= 5
    )
    return metric, Image.fromarray(np.clip(delta * 4, 0, 255).astype("uint8"))


def referenced_bounds(parts: list) -> list:
    points = []
    for part in parts:
        p = np.asarray(part["positions"], dtype=float).reshape(-1, 3)
        indices = np.asarray(part["indices"], dtype=int)
        if len(indices):
            points.append(p[np.unique(indices)])
    if not points:
        raise ValueError("Missing referenced geometry")
    values = np.concatenate(points)
    if not np.isfinite(values).all():
        raise ValueError("Invalid geometry")
    return [values.min(axis=0).tolist(), values.max(axis=0).tolist()]


def validate_capture(game: Path, runtime: dict | None = None) -> dict:
    record = json_read(game / "capture-inputs.json")
    if record.get("schema") != "eve.tests.bundled-capture-inputs" or record.get("version") != 1:
        raise ValueError("Unsupported capture input evidence")
    if runtime is not None and record.get("runtime") != runtime:
        raise ValueError("Capture runtime changed or was not recorded")
    if sha(game / "frame.png") != record["frameSha256"]:
        raise ValueError("Captured frame changed")
    for name, digest in record["inputs"].items():
        if Path(name).name != name or sha(game / name) != digest:
            raise ValueError("Capture input changed")
    if not all(name in record["inputs"] for name in ("scene.glb", "main.nut", "config.nut")):
        raise ValueError("Incomplete captured scene inputs")
    return record


def validate_derivation(record: dict, scene: dict, generator_sha: str, original: dict) -> None:
    if record.get("schema") != "eve.tests.bundled-derivation" or record.get("version") != 1:
        raise ValueError("Unsupported derived-example provenance")
    if record["generatorSha256"] != generator_sha:
        raise ValueError("Derived-example generator changed")
    if record["sourceScene"] != scene["scene"] or scene["sceneSha256"] != original["sceneSha256"]:
        raise ValueError("Derived-example original scene changed")
    originals = [road for road in original["roads"] if road["name"] == record["sourceRoad"]]
    if len(originals) != 1 or record["originalMarkers"] != [
        m["position"] for m in originals[0]["markers"]
    ]:
        raise ValueError("Derived-example original author inputs changed")
    if len(scene["roads"]) != 1 or record["markers"] != [
        m["position"] for m in scene["roads"][0]["markers"]
    ]:
        raise ValueError("Derived-example author inputs changed")


def package_sources(package: Path, project: Path, paths: set) -> dict:
    result = {}
    with tarfile.open(package, "r:gz") as archive:
        members = {m.name: m for m in archive.getmembers()}
        for name, member in members.items():
            if not name.endswith("/pathname"):
                continue
            path = archive.extractfile(member).read().decode("utf-8").splitlines()[0]
            if path in paths:
                original = archive.extractfile(members[name.rsplit("/", 1)[0] + "/asset"]).read()
                digest = hashlib.sha256(original).hexdigest()
                if sha(project / path) != digest:
                    raise ValueError("Resource differs from original package: " + path)
                result[path] = digest
    if set(result) != paths:
        raise ValueError("Missing original package resources")
    return result


def publish(args: argparse.Namespace) -> None:
    args.gallery.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "eve.tests.bundled-example-correspondence",
        "version": 1,
        "runtime": road_parity.renderer_runtime_hashes(args.eve),
        "generatorSha256": sha(args.generator),
        "sourceSha256": {
            str(p): sha(p)
            for p in (
                Path(__file__),
                Path("scripts/road_bundled_alignment.py"),
                Path("scripts/road_parity/UnityBundledSceneExport.cs"),
                Path("scripts/road_parity/UnityBundledRuntimeCapture.cs"),
                Path("scripts/road_parity/UnityBundledVariants.cs"),
                Path("scripts/road_parity/capture_bundled.ps1"),
                Path("scripts/road_parity/native/BundledRoads.cpp"),
                Path("scripts/road_parity/material/BundledMaterial.frag"),
                Path("src/modules/procgen/spline/SplineCircularArc.cpp"),
                Path("src/modules/procgen/spline/SplinePath.cpp"),
                Path("src/modules/procgen/road/RoadAlgorithmRevision.h"),
            )
        },
        "scenes": [],
        "fullyAligned": False,
        "remaining": [
            "Original Standard/Gamma material port still differs in BRDF, reflections, normal sampling and shadow filtering",
            "Junction/ring meshes are shared reference context, not independent native junction generation",
            "Original section UV atlas is bound by a fixture adapter, not a new production UV contract",
            "Terrain resources are reused; terrain shader/LOD/basemap and sky rendering are not pixel-identical",
            "Far-view mask raster/coverage differences remain; 1px-band count is diagnostic, never substitutes the unchanged IoU threshold",
        ],
    }
    cards = []
    original_paths = set()
    for baseline in Path("build/road-parity/bundled-examples").glob("*/evidence.json"):
        for dependency in json_read(baseline)["dependencies"]:
            if dependency["path"].startswith(("Assets/EasyRoads3D/", "Assets/EasyRoads3D scenes/")):
                original_paths.add(dependency["path"])
    for number, source in enumerate(sorted(args.root.glob("*/scene.json"))):
        directory = source.parent
        scene = json_read(source)
        binding = json_read(directory / "binding-evidence.json")
        if binding.get("schema") != "eve.tests.bundled-bindings" or binding.get("version") != 1:
            raise ValueError("Unsupported binding evidence")
        if scene["schema"] != "eve.tests.easyroads-bundled-interchange" or scene["version"] != 1:
            raise ValueError("Unknown scene interchange")
        if sha(args.project / scene["scene"]) != scene["sceneSha256"]:
            raise ValueError("Original scene changed")
        if sha(Path("scripts/road_parity/UnityBundledSceneExport.cs")) != scene["adapterSha256"]:
            raise ValueError("Old export adapter")
        original_paths.add(scene["scene"])
        sdk = next(
            (
                p
                for p in (args.project / "Assets/EasyRoads3D").rglob("*.dll")
                if sha(p) == scene["referenceSha256"]
            ),
            None,
        )
        if sdk is None:
            raise ValueError("SDK assembly changed")
        original_paths.add(sdk.relative_to(args.project).as_posix())
        if binding["sceneSha256"] != sha(source) or binding["nativeRoadsSha256"] != sha(
            directory / "native-roads.json"
        ):
            raise ValueError("Baked scene binding changed")
        ids = [road["id"] for road in scene["roads"]]
        if len(ids) != len(set(ids)) or set(binding["roads"].values()) != set(ids):
            raise ValueError("Lost or duplicated road identity")
        record = {
            "name": directory.name,
            "sourceSha256": sha(source),
            "originalSceneSha256": scene["sceneSha256"],
            "views": {},
            "roadCount": len(ids),
            "roads": [],
            "resourceSha256": {},
            "inputs": {
                "lights": scene["lights"],
                "ambientAuthor": scene["ambient"],
                "ambientIrradiance": scene["ambientIrradiance"],
                "ambientShaderGamma": shader_ambient(scene),
                "msaaSamples": scene["msaaSamples"],
                "cameras": scene["views"],
            },
        }
        workflow = directory / "workflow.json"
        if workflow.exists():
            record["workflow"] = json_read(workflow)
        derivation_path = directory / "derivation.json"
        if derivation_path.exists():
            derivation = json_read(derivation_path)
            validate_derivation(
                derivation,
                scene,
                sha(Path("scripts/road_parity/UnityBundledVariants.cs")),
                json_read(Path("build/road-parity/bundled-alignment/scene main/scene.json")),
            )
            record["derivation"] = derivation
            record["derivationSha256"] = sha(derivation_path)
        native = json_read(directory / "native-roads.json")
        if native.get("schema") != "eve.tests.bundled-native-roads" or native.get("version") != 1:
            raise ValueError("Unsupported native bake evidence")
        for road in scene["roads"]:
            key = next(k for k, v in binding["roads"].items() if v == road["id"])
            baked = next(r for r in native["roads"] if r["id"] == key)
            reference_parts = [
                p for p in scene["parts"] if p["role"] == "road-output" and p["road"] == road["id"]
            ]
            a, b = (
                np.asarray(referenced_bounds(reference_parts)),
                np.asarray(referenced_bounds(baked["parts"])),
            )
            record["roads"].append(
                {
                    "name": road["name"],
                    "id": road["id"],
                    "markers": len(road["markers"]),
                    "referenceBounds": a.tolist(),
                    "nativeBounds": b.tolist(),
                    "maxBoundsDeltaMeters": float(np.max(abs(a - b))),
                    "canonicalNetworkSha256": sha(directory / "networks" / (key + ".canonical")),
                }
            )
            previous_native = (
                args.root.parent
                / "bundled-variants-before-v22"
                / directory.name
                / "native-roads.json"
            )
            if "derivation" in record and previous_native.exists():
                previous = next(r for r in json_read(previous_native)["roads"] if r["id"] == key)
                record["roads"][-1]["previousMaxBoundsDeltaMeters"] = float(
                    np.max(abs(a - np.asarray(referenced_bounds(previous["parts"]))))
                )
                record["previousNativeSha256"] = sha(previous_native)
        for material in scene["materials"]:
            if material["path"] and (args.project / material["path"]).is_file():
                original_paths.add(material["path"])
            for texture in material["textures"]:
                p = args.project / texture["path"]
                if p.is_file():
                    original_paths.add(texture["path"])
                    record["resourceSha256"][str(p)] = sha(p)
        for terrain in scene["terrains"]:
            for layer in terrain["layers"]:
                for property in ("diffuse", "normal"):
                    if layer[property]:
                        original_paths.add(layer[property])
                        record["resourceSha256"][layer[property]] = sha(
                            args.project / layer[property]
                        )
        panels = []
        for view in scene["views"]:
            name = view["name"]
            reference = directory / ("unity-" + name + ".png")
            reference_mask = directory / ("unity-" + name + "-road-mask.png")
            figures = []
            masks = []
            record["views"][name] = {}
            target = args.gallery / f"bundled-{number}-{name}-unity.png"
            shutil.copyfile(reference, target)
            reference_label = (
                "Unity 原插件生成 / 原材质"
                if "derivation" in record
                else "Unity 原示例 / 原 shader"
            )
            figures.append(
                f'<figure><figcaption>{reference_label}</figcaption><a href="{target.name}"><img src="{target.name}"></a></figure>'
            )
            for mode, label in (
                ("calibration", "EVEngine 同几何渲染校准"),
                ("generated", "EVEngine 独立道路生成"),
            ):
                game = args.root / "games" / (directory.name + "-" + mode + "-" + name)
                candidate = game / "frame.png"
                candidate_mask = game.with_name(game.name + "-mask") / "frame.png"
                for folder in (game, candidate_mask.parent):
                    validate_capture(folder, report["runtime"])
                    if "[capture] OK" not in (folder / "capture.log").read_text(encoding="utf-8"):
                        raise ValueError("Missing engine-owned capture")
                metric, diff = compare(reference, candidate, reference_mask, candidate_mask)
                record["views"][name][mode] = metric
                target = args.gallery / f"bundled-{number}-{name}-{mode}.png"
                shutil.copyfile(candidate, target)
                figures.append(
                    f'<figure><figcaption>{label}</figcaption><a href="{target.name}"><img src="{target.name}"></a></figure>'
                )
                mask_target = args.gallery / f"bundled-{number}-{name}-{mode}-mask.png"
                shutil.copyfile(candidate_mask, mask_target)
                masks.append(
                    f'<figure><figcaption>{label} · 道路掩码</figcaption><img src="{mask_target.name}"></figure>'
                )
                if mode == "generated":
                    diff_target = args.gallery / f"bundled-{number}-{name}-diff.png"
                    diff.save(diff_target)
                    figures.append(
                        f'<figure><figcaption>独立生成差值 ×4</figcaption><a href="{diff_target.name}"><img src="{diff_target.name}"></a></figure>'
                    )
                metric["gameFilesSha256"] = {
                    p.name: sha(p)
                    for p in game.iterdir()
                    if p.is_file()
                    and p.suffix in (".nut", ".glb", ".png", ".spv")
                    and p.name != "mesh_data.nut"
                }
            a, b = record["views"][name]["calibration"], record["views"][name]["generated"]
            ref_target = args.gallery / f"bundled-{number}-{name}-unity-mask.png"
            shutil.copyfile(reference_mask, ref_target)
            panels.append(
                f'<h3>{name}</h3><p>同几何校准：整图 RGB MAE {a["fullRgbMAE"]:.3f}/255，道路掩码 IoU {a["roadMaskIoU"]:.4f}。独立生成：整图 MAE {b["fullRgbMAE"]:.3f}/255，掩码 IoU {b["roadMaskIoU"]:.4f}，道路内部 MAE {b["roadInteriorRgbMAE"]}/255。{"图像指标达到阈值，仍有下述未验证范围。" if b["imageThresholdsMet"] else "尚未通过既有图像阈值。"}</p><section>{"".join(figures)}</section><details><summary>道路专用掩码，排除地形和共享路口</summary><section class="masks"><figure><figcaption>Unity</figcaption><img src="{ref_target.name}"></figure>{"".join(masks)}</section></details>'
            )
            previous_game = (
                args.root.parent
                / "bundled-variants-before-v22"
                / "games"
                / f"{directory.name}-generated-{name}"
            )
            if "derivation" in record and (previous_game / "frame.png").exists():
                previous_mask = previous_game.with_name(previous_game.name + "-mask")
                for folder in (previous_game, previous_mask):
                    if (
                        sha(folder / "frame.png")
                        != json_read(folder / "capture-inputs.json")["frameSha256"]
                    ):
                        raise ValueError("Previous variant frame changed")
                old_inputs = json_read(previous_game / "capture-inputs.json")["inputs"]
                new_inputs = json_read(
                    args.root / "games" / previous_game.name / "capture-inputs.json"
                )["inputs"]
                if {k: v for k, v in old_inputs.items() if k != "scene.glb"} != {
                    k: v for k, v in new_inputs.items() if k != "scene.glb"
                }:
                    raise ValueError("Variant before/after materials or camera script changed")
                before_metric, _ = compare(
                    reference,
                    previous_game / "frame.png",
                    reference_mask,
                    previous_mask / "frame.png",
                )
                before_metric["captureManifestSha256"] = sha(previous_game / "capture-inputs.json")
                before_metric["captureRuntime"] = json_read(previous_game / "capture-inputs.json")[
                    "runtime"
                ]
                record["views"][name]["beforeFix"] = before_metric
                old_target = args.gallery / f"bundled-{number}-{name}-before-v22.png"
                shutil.copyfile(previous_game / "frame.png", old_target)
                panels.append(
                    f'<h3>负引入修复前后 · {name}</h3><p>道路掩码 IoU {before_metric["roadMaskIoU"]:.4f} → {b["roadMaskIoU"]:.4f}；相同材料/灯光/相机。</p><section class="masks"><figure><figcaption>Unity</figcaption><img src="bundled-{number}-{name}-unity.png"></figure><figure><figcaption>EVEngine v21</figcaption><img src="{old_target.name}"></figure><figure><figcaption>EVEngine v22</figcaption><img src="bundled-{number}-{name}-generated.png"></figure></section>'
                )
        road_rows = "".join(
            f"<tr><td>{html.escape(r['name'])}</td><td>{r['markers']}</td><td>{str(round(r['previousMaxBoundsDeltaMeters'], 4)) + ' → ' if 'previousMaxBoundsDeltaMeters' in r else ''}{r['maxBoundsDeltaMeters']:.4f}m</td><td><small>{html.escape(r['id'])}</small></td></tr>"
            for r in record["roads"]
        )
        origin = (
            f"原包主场景道路 {html.escape(record['derivation']['sourceRoad'])} 的受控派生：{html.escape(record['derivation']['variant'])}；不是额外官方示例。保留原截面/材质，仅本变体道路可见，地形先克隆。"
            if "derivation" in record
            else "主场景与两个运行时示例来自原包；Play 结果使用自带 runtimeScript，地形数据先克隆。"
        )
        cards.append(
            f"<article><h2>{html.escape(directory.name)}</h2><p>{len(ids)} 条作者道路全部映射。{origin}道路由生产生成器独立烘焙并通过保存恢复网格相等检查。</p>{''.join(panels)}<details><summary>逐条道路数据检查</summary><table><tr><th>原道路</th><th>控制点</th><th>包围盒最大差（非表面距离）</th><th>唯一层级标识</th></tr>{road_rows}</table></details></article>"
        )
        report["scenes"].append(record)
    report["packageSha256"] = sha(args.package)
    report["originalPackageResources"] = package_sources(args.package, args.project, original_paths)
    probe_path = args.root / "circular-probe.json"
    if probe_path.exists():
        probe = json_read(probe_path)
        if probe.get("schema") != "eve.tests.bundled-circular-probe" or probe.get("version") != 1:
            raise ValueError("Unsupported circular probe evidence")
        arrays = [
            np.asarray([[v[k] for k in ("x", "y", "z")] for v in probe[name]])
            for name in ("originalCenters", "isolatedCenters")
        ]
        bounds = [np.stack([a.min(axis=0), a.max(axis=0)]) for a in arrays]
        report["circularProbe"] = {
            "evidenceSha256": sha(probe_path),
            "sourceSha256": sha(Path("scripts/road_parity/UnityBundledCurveProbe.cs")),
            "controls": probe["controls"],
            "sdkOriginalVsIsolatedCenterBoundsDeltaMeters": float(
                np.max(abs(bounds[0] - bounds[1]))
            ),
            "scope": "bounds only; sample phase and connection-end details differ; not pointwise equality or a native circular pass",
        }
    improvement = []
    before_path = args.root.parent / "bundled-before-circular-v21" / "comparison.json"
    if before_path.exists() and any(s["name"] == "scene main" for s in report["scenes"]):
        before = json_read(before_path)
        old = next(s for s in before["scenes"] if s["name"] == "scene main")
        new = next(s for s in report["scenes"] if s["name"] == "scene main")
        if old["inputs"] != new["inputs"] or old["sourceSha256"] != new["sourceSha256"]:
            raise ValueError("Before/after author, lighting or camera inputs changed")
        report["circularBeforeAfter"] = {"baselineSha256": sha(before_path), "views": {}}
        for view in ("overview", "detail"):
            a = old["views"][view]["generated"]
            b = new["views"][view]["generated"]
            if any(a[k] != b[k] for k in ("referenceSha256", "referenceMaskSha256")):
                raise ValueError("Before/after Unity reference changed")
            old_resources = {
                k: v for k, v in a["gameFilesSha256"].items() if k not in ("frame.png", "scene.glb")
            }
            new_resources = {
                k: v for k, v in b["gameFilesSha256"].items() if k not in ("frame.png", "scene.glb")
            }
            if old_resources != new_resources:
                raise ValueError("Before/after materials, shader or scene script changed")
            source = before_path.parent / "games" / f"scene main-generated-{view}" / "frame.png"
            if sha(source) != a["candidateSha256"]:
                raise ValueError("Before/after baseline frame changed")
            target = args.gallery / f"bundled-circular-before-{view}.png"
            shutil.copyfile(source, target)
            report["circularBeforeAfter"]["views"][view] = {
                "beforeIoU": a["roadMaskIoU"],
                "afterIoU": b["roadMaskIoU"],
                "beforeFrameSha256": a["candidateSha256"],
                "afterFrameSha256": b["candidateSha256"],
            }
            improvement.append(
                f'<h3>Circular 修复前后 · {view}</h3><p>相同原始输入、材质、光照和相机；道路掩码 IoU {a["roadMaskIoU"]:.4f} → {b["roadMaskIoU"]:.4f}。仍未达到全部图像阈值。</p><section class="masks"><figure><figcaption>Unity 原示例</figcaption><img src="bundled-0-{view}-unity.png"></figure><figure><figcaption>EVEngine 修复前</figcaption><img src="{target.name}"></figure><figure><figcaption>EVEngine 修复后</figcaption><img src="bundled-0-{view}-generated.png"></figure></section>'
            )
    (args.root / "comparison.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    page = """<!doctype html><meta charset="utf-8"><title>EasyRoads 原始示例与 EVEngine 对应</title><style>body{background:#141b24;color:#e4edf7;font:15px system-ui;margin:24px}p{line-height:1.8}section{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.masks{grid-template-columns:repeat(3,1fr)}figure{margin:0}img{width:100%}article{margin:45px 0}a{color:#8fcaff}aside{border:1px solid #e8ad55;padding:16px}td,th{text-align:left;padding:8px;border-bottom:1px solid #364353}table{width:100%;border-collapse:collapse}summary{cursor:pointer;margin:18px 0}</style><h1>EasyRoads 自带示例 · EVEngine 对应与差异</h1><aside>尚未完整对齐。Unity保留原场景、资源、材质和灯光；EVEngine使用相同源纹理、作者材料参数、灯光输入和相机，Standard/Gamma着色移植仍有差异，不能声称shader逐像素等价。同几何列仅验证渲染导入；独立生成列才替换道路网格。路口/环岛网格与地形仍作为共享资源使用，未证明原生路口及地形生成对齐。</aside><p>本轮修复：同名道路身份冲突、行车道/步道宽度语义、深度偏移导致的地形穿插、mipmap/重复采样、按源码保留Gamma环境光转换、地形Legacy Lambert路径（去除误加高光）、UV翻转后的法线基底、原网格切线传递、MSAA/额外FXAA配置。原始场景文件未改动。实际Shader/阴影过滤、天空、地形LOD和原生连接端口仍需继续修复。</p><p>3个原示例的4种状态：主示例33条、已有路网编辑1条、已有路网Play2条、新建路网Play1条；不是37个独立示例。每种状态两个相机，含Unity原图、两条EVEngine路径及各自道路掩码。道路阈值保持IoU≥.98、内部MAE≤2/255、P95≤5/255。远景细线受采样影响，整图和掩码结果均保留；不修改阈值。不以地形、共享连接面积稀释道路误差。</p><p><a href="bundled-examples.html">之前的原包基线</a> · <a href="options.html?revision=usage-audit">旧自制诊断夹具（非验收）</a></p>"""
    derived_count = sum("derivation" in scene for scene in report["scenes"])
    if derived_count == len(report["scenes"]) and derived_count:
        page = page.replace(
            "EasyRoads 自带示例 · EVEngine 对应与差异", "EasyRoads 原包道路派生场景 · 双端一致性"
        )
        page = page.replace(
            "Unity保留原场景、资源、材质和灯光",
            "Unity在原包道路实例上修改作者参数，保留原截面、资源、材质和灯光",
        )
        start = page.index("<p>3个原示例")
        end = page.index("</p>", start) + 4
        page = (
            page[:start]
            + (
                f"<p>{derived_count}类原包道路受控派生场景：三种出弯方向、镜像、半尺寸、上坡、下坡、倾斜路面；不是额外官方示例。原截面与材质保留，连接上下文隐藏，地形克隆。每场景两相机，分别比较同几何校准与独立道路生成。阈值保持IoU≥.98、内部MAE≤2/255、P95≤5/255。</p>"
            )
            + page[end:]
        )
        page = page.replace('href="bundled-examples.html"', 'href="../bundled-examples.html"')
        page = page.replace('href="options.html?', 'href="../options.html?')
    (args.gallery / "bundled-alignment.html").write_text(
        page
        + "<p>新增 Circular 修复：依据原包公共 API 探针，支持直线引入及双端切向约束；不可实现的负引入使用直线区间，生产算法身份 v22。包围盒差不是表面距离，未覆盖的方向组合仍需继续验证。</p>"
        + "".join(improvement)
        + "".join(cards),
        encoding="utf-8",
    )
    print(
        f"Published {len(report['scenes'])} example/derived states; explicit remaining differences"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for key, value in {
        "root": "build/road-parity/bundled-alignment",
        "project": "build/road-parity/unity-project",
        "gallery": "build/road-parity/gallery-35",
        "eve": "build/road-parity/host-profile/src/engine/eve.exe",
        "generator": "build/road-parity/native-verified/road_bundled_fixture.exe",
        "package": "C:/baidunetdiskdownload/Unity3D_EasyRoads3D Pro v3_Vv3.2.4f5_道路设施地形创建工具/EasyRoads3D Pro v3 v3.2.4f5.unitypackage",
    }.items():
        parser.add_argument("--" + key, type=Path, default=Path(value))
    publish(parser.parse_args())


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
