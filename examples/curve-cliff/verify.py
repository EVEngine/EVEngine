"""Verify a running cliff tool through real retained UI events (no script eval).

Run eve run --mcp-port=7548, then python examples/curve-cliff/verify.py.
Uses the repository's existing newline-JSON MCP client. Saves the test document
via the tool's Save button, replacing its previous curve-cliff-v1.json save.
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
from PIL import Image, ImageChops, ImageStat


def main():
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location(
        "editor_mcp_client", root / "examples/ai-editor/editor_demo.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.McpClient("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 7548)
    client.call("initialize", {"protocolVersion": "2025-06-18",
                "clientInfo": {"name": "curve-cliff-verifier", "version": "1"}})

    def tool(name, args=None):
        return json.loads(client.tool(name, args))

    def status():
        return tool("eve_ui_get", {"host": "cliff", "widget": "status"})["text"]

    def click(widget):
        result = tool("eve_ui_click", {"host": "cliff", "widget": widget})
        assert result.get("queued"), result
        time.sleep(0.7)
        text = status()
        assert "rejected" not in text and "unchanged" not in text, text
        return text

    def capture_frame(path):
        capture = tool("eve_screenshot", {"path": path})
        if not capture.get("ok"):
            time.sleep(0.3)
            capture = tool("eve_screenshot", {"path": path})
        assert capture.get("ok"), capture
        return capture

    def pixels(capture):
        with Image.open(capture["path"]) as image:
            return image.convert("RGB").crop((420, 240, 890, 470))

    session = tool("eve_agent_session_start", {
        "objective": "Verify curve cliff authoring through UI and native frame capture",
        "criteria": [{"id": "editing", "description": "Seed, closure, curve controls, undo and redo work"},
                     {"id": "save", "description": "Save and load recover the generated cliff"},
                     {"id": "visual", "description": "Engine-owned frame captured"}]})["sessionId"]
    for phase in ["modify", "run", "observe"]:
        tool("eve_agent_session_advance", {"sessionId": session, "phase": phase})
    baseline = click("reset")
    baseline_pixels = pixels(capture_frame("verification-baseline.png"))
    changed = click("seed")
    variant_pixels = pixels(capture_frame("verification-variant.png"))
    assert changed != baseline, (baseline, changed)
    assert click("undo") == baseline
    restored_pixels = pixels(capture_frame("verification-restored.png"))
    restored_error = max(ImageStat.Stat(ImageChops.difference(baseline_pixels, restored_pixels)).mean)
    variation_error = max(ImageStat.Stat(ImageChops.difference(baseline_pixels, variant_pixels)).mean)
    assert restored_error < 0.5, restored_error
    assert variation_error > 1.0, variation_error
    assert click("redo") == changed
    closed = click("closed")
    assert closed != changed
    assert click("undo") == changed
    saved = click("save")
    assert "Saved" in saved, saved
    click("seed")
    assert click("load") == changed
    # Use retained curve controls, independent of polled OS mouse state.
    queued=tool("eve_ui_click", {"host":"curve","widget":"front"})
    assert queued.get("queued"), queued
    time.sleep(0.8)
    moved=status()
    assert moved!=changed and "unchanged" not in moved, moved
    assert click("undo")==changed
    shortened=click("remove")
    assert shortened!=changed, shortened
    assert click("undo")==changed
    queued=tool("eve_ui_click", {"host":"curve","widget":"insert"})
    assert queued.get("queued"), queued
    time.sleep(0.8)
    selected=tool("eve_ui_get", {"host":"curve","widget":"selected"})["text"]
    assert selected.endswith("/ 6"), selected
    assert "unchanged" not in status(), status()
    assert click("undo")==changed
    for criterion in ["editing", "save"]:
        tool("eve_agent_session_evidence", {"sessionId": session, "criterionId": criterion,
             "kind": "test", "status": "pass", "summary": "Native UI event assertions passed"})
    capture = tool("eve_screenshot", {"path": "preview.png"})
    if not capture.get("ok"):
        time.sleep(0.3)
        capture = tool("eve_screenshot", {"path": "preview.png"})
    assert capture.get("ok"), capture
    tool("eve_agent_session_evidence", {"sessionId": session, "criterionId": "visual",
         "kind": "screenshot", "status": "pass", "summary": "Native rendered frame",
         "artifact": capture["path"]})
    tool("eve_agent_session_advance", {"sessionId": session, "phase": "verify"})
    completion = tool("eve_agent_session_complete", {"sessionId": session,
                      "summary": "UI interaction, persistence and native capture passed"})
    print(json.dumps({"baseline": baseline, "changed": changed, "closed": closed,
                      "moved": moved, "shortened": shortened, "physicalDragVerified": False,
                      "restoredPixelMeanError": restored_error, "seedPixelMeanDifference": variation_error,
                      "capture": capture, "session": completion}, indent=2))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
