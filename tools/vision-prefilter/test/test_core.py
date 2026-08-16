"""Unit tests for deterministic parts (rules + JSON parsing) that need no torch.

Run:  python -m pytest tools/vision-prefilter/test -q
"""

from __future__ import annotations

from vision_prefilter.model import parse_model_result
from vision_prefilter.protocol import ModelResult, ProblemRegion, ProblemType
from vision_prefilter.rules import evaluate


def test_parse_clean_json():
    raw = '{"risk_score": 3, "has_problem": true, "problem_regions": [{"bbox": [1,2,3,4], "type": "道路遮挡"}], "need_high_precision_review": true}'
    out = parse_model_result(raw)
    assert out["risk_score"] == 3
    assert out["has_problem"] is True
    assert out["need_high_precision_review"] is True
    assert out["problem_regions"][0]["type"] == "道路遮挡"


def test_parse_tolerates_fence_and_prose():
    raw = 'Sure! Here is the result:\n```json\n{"risk_score": 2, "has_problem": true, "problem_regions": [], "need_high_precision_review": false}\n```'
    out = parse_model_result(raw)
    assert out["risk_score"] == 2


def test_parse_clamps_risk():
    out = parse_model_result('{"risk_score": 99, "has_problem": true, "problem_regions": [], "need_high_precision_review": true}')
    assert out["risk_score"] == 3


def test_parse_rejects_non_json():
    import pytest

    with pytest.raises(ValueError):
        parse_model_result("the scene looks fine overall and balanced")


def test_rule_road_occlusion_high_risk():
    geom = "road=(0,0)->(100,0)"
    out = evaluate(geom, "", [])
    assert out.risk_score == 3
    assert out.has_problem is True
    assert out.need_high_precision_review is True


def test_rule_vegetation_cluster_medium_risk():
    geom = "treeCluster N=6@(10,10)"
    out = evaluate(geom, "", [])
    assert out.risk_score == 2
    assert out.has_problem is True
    assert out.need_high_precision_review is False


def test_rule_balanced_no_risk():
    out = evaluate("", "", [])
    assert out.risk_score == 0
    assert out.has_problem is False


def test_rule_model_regions_bump():
    region = ProblemRegion(bbox=[0, 0, 10, 10], type=ProblemType.OCCLUSION)
    out = evaluate("", "", [region])
    assert out.risk_score == 3
    assert out.need_high_precision_review is True
