"""Publish preserved EasyRoads bundled-example reference frames, without parity scoring."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import shutil
import tarfile
from pathlib import Path

from PIL import Image

import utf8_stdio


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def publish(args: argparse.Namespace) -> None:
    args.gallery.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.package, "r:gz") as archive:
        members = {m.name: m for m in archive.getmembers()}
        original = {}
        for name, member in members.items():
            if name.endswith("/pathname"):
                path = archive.extractfile(member).read().decode("utf-8").splitlines()[0]
                if path.endswith(".unity"):
                    original[path] = hashlib.sha256(
                        archive.extractfile(members[name.rsplit("/", 1)[0] + "/asset"]).read()
                    ).hexdigest()
    cards, records = [], []
    for evidence_path in sorted(args.root.glob("*/evidence.json")):
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        if evidence["schema"] != "eve.tests.easyroads-bundled-example" or evidence["version"] != 1:
            raise ValueError("Unsupported example evidence")
        scene = evidence["scene"]
        if (
            original.get(scene) != evidence["sourceSha256"]
            or sha(args.project / scene) != original[scene]
        ):
            raise ValueError("Example differs from supplied package")
        if evidence["adapterSha256"] != sha(
            Path("scripts/road_parity/UnityBundledExamplesCapture.cs")
        ):
            raise ValueError("Adapter changed after capture")
        for dependency in evidence["dependencies"]:
            if sha(args.project / dependency["path"]) != dependency["sha256"]:
                raise ValueError("Original scene dependency changed after capture")
        images = []
        for view, key in (("overview", "perspectiveSha256"), ("detail", "detailSha256")):
            if evidence.get(key):
                frame = evidence_path.parent / (view + ".png")
                if sha(frame) != evidence[key] or Image.open(frame).size != (1280, 720):
                    raise ValueError("Invalid reference frame")
                target = args.gallery / (
                    "bundled-" + evidence_path.parent.name + "-" + view + ".png"
                )
                shutil.copyfile(frame, target)
                images.append(
                    f'<figure><figcaption>{view}</figcaption><a href="{html.escape(target.name)}"><img src="{html.escape(target.name)}"></a></figure>'
                )
        cards.append(
            f"<article><h2>{html.escape(Path(scene).stem)}</h2><p>原包场景 SHA256 一致；实际三角形 {evidence['triangles']}；缺材质 {evidence['missingMaterials']}；不支持的 shader {evidence['unsupportedShaders']}。</p><p>{html.escape('；'.join(evidence['components']))}</p><section>{''.join(images)}</section><p>{html.escape(evidence['status'])}</p></article>"
        )
        records.append(evidence)
    if len(records) != len(original):
        raise ValueError("Not all supplied example scenes inventoried")
    (args.root / "manifest.json").write_text(
        json.dumps(
            {
                "schema": "eve.tests.easyroads-bundled-manifest",
                "version": 1,
                "packageSha256": sha(args.package),
                "scenes": records,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    page = """<!doctype html><meta charset="utf-8"><title>EasyRoads 随包原始示例基线</title><style>body{background:#141b24;color:#e4edf7;font:16px system-ui;margin:24px}p{line-height:1.7}section{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%}a{color:#8fcaff}article{margin:40px 0}</style><h1>EasyRoads 随包原始示例 · 新参考基线</h1><p>直接打开用户提供 unitypackage 中的原始场景，逐个校验场景与包内字节一致。保留原有模型、材质、shader、灯光、地形和道路配置。只在内存中加入取景相机，没有重新生成道路，没有用自制拼装件替代，没有修改或保存原始场景。本页保留最初的 Unity 编辑态参考，不评分。主示例及两个运行时示例的真实 Play 结果、EVEngine 对应与差异已在新页面发布。<a href="bundled-alignment.html">查看原示例双端对应与逐项差异</a>。</p><p>主示例包含33条道路及T/X路口与环岛。运行时示例的编辑状态不代表其运行结果；空白的new road network编辑状态不能计为缺功能；其官方脚本的真实Play验证已在新对照页完成。当前包内未找到独立路灯、长椅、桥梁源模型prefab，不以自制资源冒充官方示例。</p><p><a href="options.html?revision=usage-audit">此前自制夹具：仅作诊断、不作验收</a></p>"""
    (args.gallery / "bundled-examples.html").write_text(page + "".join(cards), encoding="utf-8")
    print(f"Published {len(records)} package-verified original scenes; no parity score")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path("build/road-parity/bundled-examples"))
    parser.add_argument("--project", type=Path, default=Path("build/road-parity/unity-project"))
    parser.add_argument("--gallery", type=Path, default=Path("build/road-parity/gallery-35"))
    publish(parser.parse_args())


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
