"""story-scene-agent unit tests (dry-run: no engine, no network, no models)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

PKG_DIR = Path(__file__).resolve().parents[1]
if str(PKG_DIR) not in sys.path:
    sys.path.insert(0, str(PKG_DIR))

REPO = Path(__file__).resolve().parents[3]
CREATIVE = REPO / "tools" / "creative-brain"
if str(CREATIVE) not in sys.path:
    sys.path.insert(0, str(CREATIVE))

from story_scene_agent.executor import MemorySceneExecutor  # noqa: E402
from story_scene_agent.orchestrator import StorySceneBrain  # noqa: E402
from story_scene_agent.story import parse_stage  # noqa: E402


def test_parse_stage_night_forest():
    brief = parse_stage("月光下的幽暗森林，树影摇曳，冒险者在木桥边遇到巨龙的宝藏")
    assert brief.timeOfDay == "night"
    assert brief.beats >= 1
    assert "tree" in {f["kind"] for f in brief.focal}
    assert brief.centerpiece is not None


def test_parse_stage_empty():
    brief = parse_stage("")
    assert brief.beats == 1
    assert brief.focal == []
    assert brief.centerpiece is None


def test_plan_build_no_llm():
    brain = StorySceneBrain({}, dry_run=True)
    stage = parse_stage("snowy tundra with scattered rocks at dusk")
    plan = brain.build_plan("snowy tundra with scattered rocks at dusk", stage)
    actions = [s.action for s in plan.steps]
    assert "terrain" in actions
    assert "set_lighting" in actions
    assert "place" in actions
    assert plan.config.lighting.timeOfDay == stage.timeOfDay


def test_memory_executor_roundtrip():
    ex = MemorySceneExecutor()
    ex.reset()
    assert ex.spawn({"id": "hero", "kind": "pillar", "pos": [1.0, 0.0, 2.0]})["ok"]
    assert ex.spawn({"id": "dragon", "kind": "rock"})["ok"]
    info = ex.info()
    assert info["count"] == 2
    assert ex.modify("move_object", "hero", {"pos": [5.0, 1.0, 5.0]})["ok"]
    assert ex.props["hero"]["pos"] == [5.0, 1.0, 5.0]
    assert ex.modify("remove_object", "dragon", {})["ok"]
    assert ex.info()["count"] == 1
    assert len(ex.cameras(3)) == 3
    assert ex.cameras(3)[0]["id"] == "cam_0"


def test_full_dryrun_loop(tmp_path):
    out_dir = str(tmp_path)
    cfg = {
        "creative": {"catalog": str(CREATIVE / "catalogs" / "assets.example.json"), "seed": 7},
        "stage": {"default_camera": {"eye": [0, 12, 22], "target": [0, 1, 0], "fov": 55}},
        "qc": {"max_rounds": 3, "targets": ["scene"]},
        "output": {"out_dir": out_dir},
    }
    brain = StorySceneBrain(cfg, dry_run=True, trace=False)
    result = brain.run("moonlit duel between a knight and a dragon before an old castle",
                       scene_id="duel", out_dir=out_dir)
    assert result["stage"]["beats"] >= 1
    assert result["plan"]["steps"]
    assert result["build"], "build log must not be empty"
    assert result["final"]["info"]["count"] > 0
    assert "passed" in result["qc"]
    assert isinstance(result["qc"]["rounds_used"], int)
    report = Path(out_dir) / brain.cfg["output"]["report"]
    assert report.exists()
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["story"]
    assert data["stage"]["atmosphere"]


def test_creative_brain_snippet_targets_kit():
    from brain.mcp import snippet_for_step  # type: ignore

    src = snippet_for_step("place", "tree", {"x": 4, "y": 5, "seed": 9})
    assert "scene_director.modify" in src
    assert "add_object" in src
    src2 = snippet_for_step("set_lighting", "scene", {"timeOfDay": "night"})
    assert "lighting" in src2