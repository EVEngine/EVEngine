#!/usr/bin/env python3
"""Verify native scene commands, reference feedback and Blender GLB live editing."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import utf8_stdio


def exercise(client, output: Path, visibility: bool = True) -> dict:
    from PIL import Image
    from scene_qc_agent.reference import ReferenceRefiner, checked

    refiner = ReferenceRefiner(client, output / "target.png", {}, output)
    refiner.modify("reset")
    refiner.modify("spawn", params={"id": "subject", "kind": "box", "pos": [0, 0, 0],
        "scale": [2, 2, 2], "tint": [1, 0, 0], "metallic": 0, "roughness": 1,
        "cast_shadow": False, "receive_shadow": False})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 55,
        "ambient": [1, 1, 1], "exposure": 0})
    refiner.modify("lighting", params={"background": [0.01, 0.01, 0.01],
        "direction": [0, -1, -1], "color": [1, 1, 1]})
    target, _ = refiner.capture("target")
    with Image.open(target) as image:
        color = image.convert("RGB").getpixel((image.width // 2, image.height // 2))
    if color[0] <= color[1] + 50 or color[0] <= color[2] + 50:
        raise AssertionError("Native reference frame must show the red subject")
    refiner.reference, refiner.mask = target, {"rgb": list(color), "tolerance": 30}
    refiner.modify("move", "subject", {"pos": [1.5, 0, 0]})
    report = refiner.run([
        [{"action": "screen", "target": "subject", "params": {"anchor": [0, 0, 0], "screen": [0.5, 0.5]}}],
        [{"action": "move", "target": "subject", "params": {"pos": [-2, 0, 0]}}],
    ])
    if not report["candidates"][0]["accepted"] or report["candidates"][1]["accepted"]:
        raise AssertionError("Feedback must accept improvement and reject regression")
    if report["final"]["metrics"]["silhouetteIou"] < 0.99:
        raise AssertionError("Fixed-camera reference was not restored")
    saved = refiner.modify("save")
    try:
        checked(client.call_tool("eve_scene_modify", {"action": "spawn", "params": {
            "id": "missing", "kind": "asset", "path": "missing.glb", "revision": "missing-v1"}}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Missing asset must fail")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Failed resource preparation changed the document")
    refiner.modify("spawn", params={"id": "imported-asset", "kind": "asset", "path": "imported.glb",
        "revision": "proof-v1", "pos": [3, 0, 0]})
    asset = next(item for item in refiner.info()["props"] if item["id"] == "imported-asset")
    if not asset["geometry"]["ok"] or max(asset["geometry"]["value"]["worldSize"]) <= 0:
        raise AssertionError("Imported geometry has no actual bounds")
    refiner.modify("undo")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Asset undo did not restore the original document")
    refiner.modify("redo")
    if refiner.info()["count"] != 2:
        raise AssertionError("Asset redo did not reconstruct geometry")
    refiner.modify("remove", "imported-asset")
    if visibility:
        exercise_visibility(refiner, output)
        exercise_visual_relations(refiner, output)
        exercise_regions(refiner, output)
        exercise_projected_overlap(refiner, output)
    exercise_relations(refiner, output, visibility)
    result = client.call_tool("eve_run_script", {"source":
        'proofLoader <- eve.SceneLoader(); proofHost <- proofLoader.load("imported.glb"); '
        'if(proofHost==null) throw "import failed";'})
    if result.strip() != "ok":
        raise AssertionError(f"GLB bootstrap failed: {result}")
    refiner.modify("bind_host", params={"host": "imported.glb"})
    live = refiner.info()
    if not live["props"]:
        raise AssertionError("Imported host has no native nodes")
    live_saved = refiner.modify("save")
    node = live["props"][0]
    position = list(node["pos"])
    position[0] += 1
    refiner.modify("move", node["id"], {"pos": position})
    refiner.modify("undo")
    if refiner.modify("save")["json"] != live_saved["json"]:
        raise AssertionError("Live imported host undo failed")
    refiner.capture("blender-live")
    (output / "import-proof.json").write_text(json.dumps({"asset": asset, "live": live}, indent=2), encoding="utf-8")
    return report["final"]["metrics"]


def exercise_visibility(refiner, output: Path) -> None:
    """Measure real Vulkan triangle/depth coverage and an independent candidate."""
    refiner.modify("reset")
    refiner.modify("spawn", params={"id": "target", "kind": "box", "pos": [0, 0, 0],
        "scale": [2, 2, 0.1], "tint": [0.9, 0.2, 0.1]})
    refiner.modify("spawn", params={"id": "blocker", "kind": "box", "pos": [4, 0, 1],
        "scale": [1.8, 3, 0.1], "tint": [0.2, 0.3, 0.9]})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    # Query failures must remain explicit and atomic through the actual script binding.
    result = refiner.client.call_tool("eve_run_script", {"source": r"""
        local entity=scene_director.visuals["target"].entities[0];
        local matrix=[2,0,0,0,0,2,0,0,0,0,0.1,0,0,0,0,1];
        local draw={object="target",entityId=entity.getEntityId(),generation=entity.getEntityGeneration(),model=matrix};
        local request={schemaId="eve.scene.visibility-request",schemaVersion=1,sourceRevision=scene_director.session.getRevision(),
            candidateGeneration=0,width=64,height=64,camera={eye=[0,0,10],target=[0,0,0],up=[0,1,0],fov=60,near=0.1,far=100},
            draws=[draw],targets=["target"]};
        local token=sd_require(eve.prepareSceneVisibility(request));
        if(!token.measure(gfx).ok || token.measure(gfx).ok) throw "query consumption failed";
        entity.setTint(0.9,0.2,0.1,0.5);
        local unsupported=sd_require(eve.prepareSceneVisibility(request)).measure(gfx);
        entity.setTint(0.9,0.2,0.1,1.0);
        if(unsupported.ok) throw "transparent geometry silently accepted";
        local other=eve.Renderable3D();other.setMesh(gfx.newMeshCube(1.0));other.setVisible(false);
        local second=clone draw;second.entityId=other.getEntityId();second.generation=other.getEntityGeneration();
        second.model=[2,0,0,0,0,2,0,0,0,0,0.1,0,2,0,0,1];request.draws.append(second);
        local combined=sd_require(sd_require(eve.prepareSceneVisibility(request)).measure(gfx));
        if(combined.objectIds.len()!=1 || combined.targets[0].visibleFraction!=1.0 || combined.targets[0].fullPixels<80)
            throw "multi-mesh object aggregation failed";
        sd_require(eve.destroyRenderable3D(other.getEntityId(),other.getEntityGeneration()));
        if(sd_require(eve.prepareSceneVisibility(request)).measure(gfx).ok) throw "stale handle accepted";
    """})
    if result.strip() != "ok":
        raise AssertionError(f"Native visibility contract failed: {result}")
    reports = []
    # Front-facing depths compensate blocker projected width; pixel tolerance covers edge quantization.
    for occlusion, x in ((0, 4), (0.25, 1.35), (0.5, 0.9), (1, 0)):
        refiner.modify("move", "blocker", {"pos": [x, 0, 1]})
        evidence = refiner.modify("visibility", "target", {"width": 256, "height": 256})["value"]
        measured = evidence["targets"][0]
        if measured["status"] != "Measured" or abs(measured["visibleFraction"] - (1 - occlusion)) > 0.035:
            raise AssertionError(f"Occlusion {occlusion} differs: {measured}")
        if occlusion and (len(measured["occluders"]) != 1 or measured["occluders"][0]["object"] != "blocker"):
            raise AssertionError("Actual frontmost occluder mapping missing")
        reports.append(evidence)
    refiner.capture("visibility-fully-blocked")
    saved = refiner.modify("save")
    model = [1.8, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.1, 0, 0, 0, -1, 1]
    candidate = refiner.modify("visibility", "target", {"models": {"blocker": model}})["value"]
    if candidate["targets"][0]["visibleFraction"] != 1 or candidate["targets"][0]["occluders"]:
        raise AssertionError("Object behind target was counted as occluder")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Candidate rendering changed authoritative document")
    refiner.capture("visibility-after-candidate")
    offscreen = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0.1, 0, 100, 0, 0, 1]
    empty = refiner.modify("visibility", "target", {"models": {"target": offscreen}})["value"]
    if empty["targets"][0]["status"] != "Unmeasurable" or empty["targets"][0]["fullPixels"]:
        raise AssertionError("Offscreen target must be unmeasurable")
    (output / "visibility-proof.json").write_text(json.dumps({"ratios": reports, "candidate": candidate,
        "offscreen": empty}, indent=2), encoding="utf-8")


def exercise_visual_relations(refiner, output: Path) -> None:
    """Persist, raster-check and commit visual intent through native MCP."""
    refiner.modify("reset")
    refiner.modify("spawn", params={"id": "target", "kind": "box", "pos": [0, 0, 0],
        "scale": [2, 2, 0.1], "tint": [0.9, 0.2, 0.1]})
    refiner.modify("spawn", params={"id": "blocker", "kind": "box", "pos": [0, 0, 1],
        "scale": [1.8, 3, 0.1], "tint": [0.2, 0.3, 0.9]})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    refiner.modify("relations", params={"value": [
        {"id": "target-visible", "type": "visible_fraction", "subject": "target", "minimum": 0.7},
        {"id": "blocker-cover", "type": "occludes", "subject": "blocker", "reference": "target",
         "minimum": 0.2, "maximum": 0.3}]})
    saved = refiner.modify("save")
    refiner.modify("relation_begin", "blocker")
    preparation = refiner.client.call_tool("eve_run_script", {"source": r"""
        local request=sd_require(scene_director.visibility("target",{})).request;
        sd_require(scene_director.session.cancelPhysicsPlacement());
        local objects=[];
        foreach(node in scene_director.hierarchy().objects) objects.append({object=node.id,
            selected=node.id=="blocker",locked=node.id!="blocker",halfExtents=[0.5,0.5,0.5]});
        sd_require(scene_director.session.beginRelationPlacement({objects=objects,mode="place",
            settings={freezeRotationX=true,freezeRotationY=true,freezeRotationZ=true}}));
        local original=request.camera.fov;request.camera.fov=original+1;
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "wrong camera accepted";
        request.camera.fov=original;
        request.sourceRevision--;
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "wrong revision accepted";
        request.sourceRevision++;
        local label=request.draws[0].object;request.draws[0].object=label=="target"?"blocker":"target";
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "wrong object mapping accepted";
        request.draws[0].object=label;
        local x=request.draws[0].model[12];request.draws[0].model[12]=x+10;
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "wrong authored model accepted";
        request.draws[0].model[12]=x;
        local last=request.draws.pop();
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "incomplete draw set accepted";
        request.draws.append(last);
        sd_require(scene_director.session.prepareRelationVisibility(scene_director.hostName,request));
        if(scene_director.session.prepareRelationVisibility(scene_director.hostName,request).ok) throw "proof was replaceable";
    """})
    if preparation.strip() != "ok":
        raise AssertionError(f"Native projection preparation contract failed: {preparation}")
    initial = refiner.modify("relation_check")["value"]
    if initial["hardSatisfied"]:
        raise AssertionError("Full occlusion accepted by hard visual constraints")
    from scene_qc_agent.reference import checked
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Native commit accepted violated visual constraints")
    good = refiner.modify("relation_update", params={"pos": [1.35, 0, 1], "ticks": 60})["value"]
    if not good["hardSatisfied"] or any(r["status"] != "Satisfied" for r in good["relations"]):
        raise AssertionError(f"Quarter-occlusion candidate did not satisfy visual intent: {good}")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Visual candidate check mutated document")
    refiner.modify("relation_commit")
    committed = refiner.modify("save")
    refiner.capture("visual-relations-committed")
    refiner.modify("undo")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Visual commit did not undo in one transaction")
    refiner.modify("redo")
    if refiner.modify("save")["json"] != committed["json"]:
        raise AssertionError("Visual commit redo differs")
    # A target outside the image has no measurable denominator and cannot satisfy hard intent.
    refiner.modify("relation_begin", "target")
    offscreen = refiner.modify("relation_update", params={"pos": [100, 0, 0], "ticks": 60})["value"]
    if offscreen["hardSatisfied"] or not any(r["status"] == "Unmeasurable" for r in offscreen["relations"]):
        raise AssertionError("Offscreen hard visual relation was accepted")
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Offscreen candidate committed")
    refiner.modify("relation_discard")
    if refiner.modify("save")["json"] != committed["json"]:
        raise AssertionError("Rejected visual preview changed the document")
    # A camera/document edit after preparation must invalidate the complete preview.
    refiner.modify("relation_begin", "blocker")
    refiner.modify("camera", params={"eye": [0, 0, 12], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Camera edit did not invalidate visual preview")
    refiner.modify("relation_discard")
    refiner.modify("undo")
    # Replacing a mesh after preparation must invalidate native resource proof.
    refiner.modify("relation_begin", "blocker")
    result = refiner.client.call_tool("eve_run_script", {"source":
        'scene_director.visuals["blocker"].entities[0].setMesh(gfx.newMeshCube(1.0));'})
    if result.strip() != "ok":
        raise AssertionError(f"Mesh replacement failed: {result}")
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Native commit accepted a changed mesh generation")
    refiner.modify("relation_discard")
    (output / "visual-relations-proof.json").write_text(
        json.dumps({"initial": initial, "candidate": good, "offscreen": offscreen, "before": json.loads(saved["json"]),
                    "committed": json.loads(committed["json"])}, indent=2), encoding="utf-8")


def exercise_projected_overlap(refiner, output: Path) -> None:
    """Compose isolated silhouettes despite depth order and a third-party blocker."""
    from scene_qc_agent.reference import checked
    refiner.modify("reset")
    for name, pos, scale in [("reference", [0, 0, 0], [2, 2, 0.1]),
                             ("subject", [8, 0, 2], [6, 6, 0.1]),
                             ("blocker", [0, 0, 4], [12, 12, 0.1])]:
        refiner.modify("spawn", params={"id": name, "kind": "box", "pos": pos, "scale": scale,
            "tint": {"reference": [0.9, 0.3, 0.1], "subject": [0.1, 0.4, 0.2], "blocker": [0.3, 0.3, 0.5]}[name]})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    refiner.modify("relations", params={"value": [
        {"id": "physical-separation", "type": "nonpenetrating", "subject": "subject", "reference": "reference",
         "tolerance": 0.001},
        {"id": "compose", "type": "projected_overlap", "subject": "subject", "reference": "reference",
         "minimum": 0.95},
        {"id": "compose-lower", "type": "projected_overlap", "subject": "subject", "reference": "reference",
         "minimum": 0.95, "region": {"schemaId": "eve.scene.local-region", "schemaVersion": 1,
             "kind": "local_box", "id": "lower", "minimum": [-0.5, -0.5, -0.6], "maximum": [0.5, 0.0, 0.6]}},
        {"id": "no-front-cover", "type": "occludes", "subject": "subject", "reference": "reference",
         "minimum": 0.0, "maximum": 0.0}]})
    saved = refiner.modify("save")
    refiner.modify("relation_begin", "subject")
    initial = refiner.modify("relation_check")["value"]
    if initial["hardSatisfied"]:
        raise AssertionError("Disjoint silhouette satisfied overlap intent")
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Projected overlap bypassed hard commit check")
    candidate = refiner.modify("relation_update", params={"pos": [0, 0, 2], "ticks": 60})["value"]
    if not candidate["hardSatisfied"]:
        raise AssertionError(f"Third-party blocker corrupted independent silhouette overlap: {candidate}")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Overlap preview mutated document")
    refiner.modify("relation_commit")
    committed = refiner.modify("save")
    refiner.capture("projected-overlap-committed")
    refiner.modify("undo")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Overlap commit did not undo atomically")
    refiner.modify("redo")
    if refiner.modify("save")["json"] != committed["json"]:
        raise AssertionError("Overlap redo diverged")
    def query():
        value = refiner.modify("visibility", "reference", {"targets": ["reference", "subject"]})["value"]
        pair = next(x for x in value["targets"][0]["projectedOverlaps"] if x["object"] == "subject")
        return value, pair
    refiner.modify("move", "subject", {"pos": [0, 0, -2]})
    behind, pair = query()
    if pair["status"] != "Measured" or pair["fraction"] != 1.0:
        raise AssertionError("Depth reversal changed full silhouette overlap")
    refiner.modify("remove", "blocker")
    behind_unblocked, pair = query()
    if pair["fraction"] != 1.0 or behind_unblocked["targets"][0]["visibleFraction"] != 1.0:
        raise AssertionError("Removing third blocker changed silhouette overlap")
    refiner.capture("projected-overlap-depth-reversed")
    refiner.modify("move", "subject", {"pos": [0, 0, 2]})
    front, pair = query()
    if pair["fraction"] != 1.0 or front["targets"][0]["visibleFraction"] != 0.0:
        raise AssertionError("Front overlap and actual occlusion mixed")
    refiner.modify("move", "subject", {"pos": [2.5, 0, -2]})
    partial, pair = query()
    if pair["status"] != "Measured" or not 0 < pair["fraction"] < 1:
        raise AssertionError("Partial silhouette intersection was not measured")
    refiner.modify("move", "subject", {"pos": [1000, 0, -2]})
    offscreen, pair = query()
    if pair["status"] != "Unmeasurable":
        raise AssertionError("Empty partner mask accepted as zero overlap")
    (output / "projected-overlap-proof.json").write_text(json.dumps({
        "initial": initial, "candidate": candidate, "behind": behind, "front": front,
        "behindUnblocked": behind_unblocked, "partial": partial,
        "offscreen": offscreen, "before": json.loads(saved["json"]), "committed": json.loads(committed["json"])},
        indent=2), encoding="utf-8")


def exercise_regions(refiner, output: Path) -> None:
    """Check object-local surface regions against actual rasterized geometry."""
    from scene_qc_agent.reference import checked
    refiner.modify("reset")
    refiner.modify("spawn", params={"id": "house", "kind": "box", "pos": [0, 0, 0],
        "scale": [2, 4, 0.1], "tint": [0.9, 0.3, 0.1]})
    refiner.modify("spawn", params={"id": "tree", "kind": "box", "pos": [0, 1.15, 1],
        "scale": [2.5, 2, 0.1], "tint": [0.1, 0.4, 0.2]})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    def local_region(name, lower, upper):
        return {"schemaId": "eve.scene.local-region", "schemaVersion": 1, "kind": "local_box",
                "id": name, "minimum": lower, "maximum": upper}
    window = local_region("window", [-0.45, 0.1, -0.6], [0.45, 0.45, 0.6])
    lower = local_region("lower-wall", [-0.5, -0.5, -0.6], [0.5, -0.1, 0.6])
    refiner.modify("relations", params={"value": [
        {"id": "window-visible", "type": "visible_fraction", "subject": "house",
         "minimum": 0.95, "region": window},
        {"id": "tree-lower-cover", "type": "occludes", "subject": "tree", "reference": "house",
         "minimum": 0.95, "region": lower}]})
    saved = refiner.modify("save")
    refiner.modify("relation_begin", "tree")
    initial = refiner.modify("relation_check")["value"]
    if initial["hardSatisfied"]:
        raise AssertionError("Tree covering window accepted regional intent")
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Regional hard constraints bypassed at commit")
    candidate = refiner.modify("relation_update", params={"pos": [0, -1.15, 1], "ticks": 60})["value"]
    if not candidate["hardSatisfied"] or any(r["status"] != "Satisfied" for r in candidate["relations"]):
        raise AssertionError(f"Window and lower-wall regions mixed: {candidate}")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Region candidate changed document")
    refiner.modify("relation_commit")
    committed = refiner.modify("save")
    refiner.capture("regions-window-visible")
    refiner.modify("undo")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Region commit undo requires more than one step")
    refiner.modify("redo")
    if refiner.modify("save")["json"] != committed["json"]:
        raise AssertionError("Region commit redo diverges")
    # Verify regions follow translation, rotation and scale together with their object.
    refiner.modify("rotation", "house", {"rotation": [0, 0, 0.5]})
    moved_region = dict(window)
    moved_region["geometryRevision"] = refiner.modify("geometry_revision", "house")["value"]
    native = refiner.modify("visibility", "house", {"targets": [
        "house", {"object": "house", "region": moved_region}]})["value"]
    if len(native["targets"]) != 2 or native["targets"][1]["status"] != "Measured":
        raise AssertionError("Rotated local region became unmeasurable")
    # Source recipe changes retain authored intent but require explicit rebinding.
    document = json.loads(refiner.modify("save")["json"])
    source = dict(document["sources"]["house"])
    source["seed"] = source.get("seed", 7) + 1
    refiner.modify("command", params={"command": "scene.authoring.sources.set.v1",
        "payload": {"object": "house", "value": source}})
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_begin", "target": "tree"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Stale region geometry binding accepted")
    refiner.modify("undo")
    refiner.modify("relation_begin", "tree")
    refiner.modify("relation_discard")
    # A box in the empty corner of a sphere's projected bounds selects no triangles.
    refiner.modify("reset")
    refiner.modify("spawn", params={"id": "sphere", "kind": "sphere", "pos": [0, 0, 0]})
    refiner.modify("camera", params={"eye": [0, 0, 10], "target": [0, 0, 0], "fov": 60, "exposure": 0})
    empty_region = local_region("empty-corner", [0.8, 0.8, -2], [1.0, 1.0, 2])
    empty_region["geometryRevision"] = refiner.modify("geometry_revision", "sphere")["value"]
    empty = refiner.modify("visibility", "sphere", {"targets": [
        "sphere", {"object": "sphere", "region": empty_region}]})["value"]
    if empty["targets"][0]["fullPixels"] < 4 or empty["targets"][1]["fullPixels"] != 0:
        raise AssertionError("Region used projected bounding rectangle instead of real surface mask")
    (output / "regions-proof.json").write_text(json.dumps({
        "initial": initial, "candidate": candidate, "transformed": native, "emptyCorner": empty,
        "before": json.loads(saved["json"]), "committed": json.loads(committed["json"])},
        indent=2), encoding="utf-8")


def exercise_relations(refiner, output: Path, capture_visibility: bool = True) -> None:
    """Check formal MCP relation preview, commit and history on the actual host."""
    refiner.modify("reset")
    for name, pos, scale in (("floor", [0, -0.5, 0], [10, 1, 10]),
                             ("crate", [0, 3, 0], [1, 1, 1]),
                             ("wall", [5, 1, 0], [1, 2, 6])):
        refiner.modify("spawn", params={"id": name, "kind": "box", "pos": pos, "scale": scale,
            "tint": [0.9, 0.25, 0.1] if name == "crate" else [0.45, 0.45, 0.5]})
    refiner.modify("camera", params={"eye": [6, 5, 10], "target": [0, 0, 0], "exposure": 0})
    refiner.modify("relations", params={"value": [
        {"id": "support", "type": "supported_on", "subject": "crate", "reference": "floor", "tolerance": 0.06},
        {"id": "wall-gap", "type": "nonpenetrating", "subject": "crate", "reference": "wall", "tolerance": 0.01}]})
    saved = refiner.modify("save")
    refiner.capture("relations-before")
    refiner.modify("relation_begin", "crate", {"mode": "fall", "expectedRevision": saved["revision"]})
    initial = refiner.modify("relation_check")["value"]
    if initial["hardSatisfied"]:
        raise AssertionError("Unstepped relation preview cannot be accepted")
    from scene_qc_agent.reference import checked
    try:
        checked(refiner.client.call_tool("eve_scene_modify", {"action": "relation_commit"}))
    except RuntimeError:
        pass
    else:
        raise AssertionError("Commit accepted unmeasured hard relations")
    measured = refiner.modify("relation_update", params={"pos": [0, 3, 0], "ticks": 240})["value"]
    if not measured["hardSatisfied"] or any(r["status"] != "Satisfied" for r in measured["relations"]):
        raise AssertionError(f"Native support/penetration relations failed: {measured}")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Physics relation preview mutated the live document")
    if capture_visibility:
        visibility = refiner.modify("visibility", "crate", {"preview": True})["value"]
        if visibility["candidateGeneration"] != measured["tick"] or not visibility["readbackComplete"]:
            raise AssertionError("Physics preview was not rendered into independent visibility evidence")
        if refiner.modify("save")["json"] != saved["json"]:
            raise AssertionError("Rendering physics candidate changed document")
    refiner.modify("relation_commit")
    committed = refiner.modify("save")
    if committed["json"] == saved["json"]:
        raise AssertionError("Relation commit did not publish candidate transforms")
    refiner.capture("relations-after")
    refiner.modify("undo")
    if refiner.modify("save")["json"] != saved["json"]:
        raise AssertionError("Relation commit did not undo in one step")
    refiner.modify("redo")
    if refiner.modify("save")["json"] != committed["json"]:
        raise AssertionError("Relation commit redo diverged")
    (output / "relations-proof.json").write_text(json.dumps({"initial": initial, "measured": measured,
        "before": json.loads(saved["json"]), "after": json.loads(committed["json"])}, indent=2), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/authoring-evidence"))
    parser.add_argument("--port", type=int, default=7540)
    parser.add_argument("--skip-visibility", action="store_true", help="Run the preexisting scene/physics control without visibility captures")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output, executable = args.output.resolve(), args.exe.resolve()
    sys.path.insert(0, str(root / "tools/scene-qc-agent"))
    from scene_qc_agent.mcp_client import EvMcpClient

    game = output / "host-game"
    game.mkdir(parents=True, exist_ok=True)
    for name in ("main.nut", "config.nut"):
        shutil.copy2(root / "examples/ai-stage" / name, game / name)
    shutil.copy2(root / "examples/blender_hot_reload/scene.glb", game / "imported.glb")
    environment = os.environ.copy()
    environment["PATH"] = str(executable.parents[2]) + os.pathsep + environment.get("PATH", "")
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    with (output / "runtime.log").open("w", encoding="utf-8") as log, \
            (output / "runtime.err").open("w", encoding="utf-8") as errors:
        process = subprocess.Popen([str(executable), "run", "--debug", f"--mcp-port={args.port}", str(game)],
            cwd=root, env=environment, stdout=log, stderr=errors, startupinfo=startup)
        client = None
        try:
            deadline = time.monotonic() + 30
            while client is None:
                if process.poll() is not None:
                    raise RuntimeError(f"Native host exited {process.returncode}; see runtime.err")
                try:
                    client = EvMcpClient({"engine": {"transport": "tcp", "host": "127.0.0.1",
                        "port": args.port, "connect_timeout_ms": 15000}})
                except OSError:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Native host MCP did not start") from None
                    time.sleep(0.1)
            client.initialize()
            metrics = exercise(client, output, not args.skip_visibility)
            print(json.dumps({"ok": True, "metrics": metrics, "output": str(output)}))
        finally:
            if client is not None:
                client.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=15)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
