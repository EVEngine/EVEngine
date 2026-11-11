#!/usr/bin/env python3
"""Exercise the real spawn editor UI, transactional history, files and clean reopen via MCP.

Uses a copied example and unique save identity. All edits go through retained UI events;
no eval or desktop framebuffer capture is used. Run with the normal window-test environment.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import utf8_stdio

KIND_WIDGET = "spawn/field/$field_k696e7374616e636573_i0_k726567696f6e_k6b696e64"
SOURCE_GUID = "01900000-0000-7000-8000-000000000041"


class EditorScenario:
    def __init__(self, root: Path, executable: Path, output: Path, port: int, client_type):
        self.executable, self.output, self.port = executable, output, port
        self.client_type = client_type
        self.client = self.process = self.log = None
        self.records: list[dict] = []
        self.save_directory: Path | None = None
        self.identity = "evengine-spawn-editor-evidence-" + str(uuid.uuid4())
        self.game = output / "game"
        shutil.copytree(
            root / "examples/spawn-editor",
            self.game,
            ignore=shutil.ignore_patterns("Content", "Saved"),
        )
        main = self.game / "main.nut"
        source = main.read_text(encoding="utf-8")
        identity_anchor = '"evengine-spawn-editor", false'
        observation_anchor = "    editorRpg = eve.RPG();"
        if source.count(identity_anchor) != 1 or source.count(observation_anchor) != 1:
            raise RuntimeError(
                "Formal example storage/observation anchors changed; update this scenario"
            )
        source = source.replace(identity_anchor, f'"{self.identity}", false')
        source = source.replace(
            observation_anchor,
            '    print("EVIDENCE_SAVE_DIRECTORY=" + fs.getSaveDirectory() + "\\n");\n'
            + observation_anchor,
        )
        main.write_text(source, encoding="utf-8")

    def call(self, name: str, args: dict | None = None) -> dict:
        args = args or {}
        try:
            result = json.loads(self.client.call_tool(name, args))
        except Exception as error:
            self.records.append({"tool": name, "args": args, "error": str(error)})
            raise
        self.records.append({"tool": name, "args": args, "result": result})
        return result

    @staticmethod
    def wait(operation, label: str, timeout: float = 30):
        deadline, last_error = time.monotonic() + timeout, None
        while True:
            try:
                value = operation()
                if value:
                    return value
            except RuntimeError as error:
                last_error = error
            if time.monotonic() >= deadline:
                raise TimeoutError(f"Timed out waiting for {label}; last error: {last_error}")
            time.sleep(0.1)

    def batch(self, actions: list[dict]) -> None:
        receipt = self.call("eve_input_batch", {"actions": actions, "waitFrames": 3})
        if not receipt.get("ok"):
            raise RuntimeError(str(receipt))
        self.wait(
            lambda: self.call("eve_input_poll", {"id": receipt["id"]}).get("status") == "ready",
            "input batch",
        )

    def click(self, host: str, widget: str) -> None:
        self.batch([{"op": "ui_click", "host": host, "widget": widget}])

    def get(self, host: str, widget: str) -> dict:
        return self.call("eve_ui_get", {"host": host, "widget": widget})

    def expect(self, host: str, widget: str, field: str, value: str) -> None:
        self.wait(lambda: self.get(host, widget).get(field) == value, f"{widget}={value}")

    def capture(self, name: str) -> None:
        self.wait(
            lambda: self.call("eve_screenshot", {"path": str(self.output / f"{name}.png")}).get(
                "ok"
            ),
            f"{name} screenshot",
        )

    def start(self, label: str) -> None:
        self.log = (self.output / f"{label}.log").open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [str(self.executable), "run", "--debug", f"--mcp-port={self.port}"],
            cwd=self.game,
            stdin=subprocess.DEVNULL,
            stdout=self.log,
            stderr=subprocess.STDOUT,
        )
        deadline = time.monotonic() + 30
        while True:
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"Native editor exited during startup: {self.process.returncode}"
                )
            try:
                self.client = self.client_type(
                    {
                        "engine": {
                            "transport": "tcp",
                            "host": "127.0.0.1",
                            "port": self.port,
                            "connect_timeout_ms": 5000,
                        }
                    }
                )
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        self.client.initialize()
        status = self.call("eve_status")
        if Path(status.get("gameRoot", "")).resolve() != self.game.resolve():
            raise RuntimeError("MCP port belongs to another game; no input will be sent")
        self.wait(lambda: self.call("eve_ui_tree").get("hostCount", 0) >= 6, "editor UI")
        for line in (self.output / f"{label}.log").read_text(encoding="utf-8").splitlines():
            if line.startswith("EVIDENCE_SAVE_DIRECTORY="):
                self.save_directory = Path(line.split("=", 1)[1])

    def stop_clean(self) -> None:
        receipt = self.call(
            "eve_ui_click", {"host": "spawn-editor/session", "widget": "spawn/session/close"}
        )
        if not receipt.get("queued"):
            raise RuntimeError(f"Close was not queued: {receipt}")
        # Do not poll a frame receipt after intentionally shutting down its server.
        self.process.wait(timeout=30)
        if self.process.returncode != 0:
            raise RuntimeError(f"Editor did not exit normally: {self.process.returncode}")
        self.client.close()
        self.client = self.process = None
        self.log.close()
        self.log = None

    def run(self) -> dict:
        self.start("edit")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "circle")
        self.click("spawn-editor/preview", "spawn/preview/stop")
        self.click("spawn-editor/session", "spawn/session/import")
        self.batch(
            [
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/path",
                    "text": "data/import-new.tiled.json",
                },
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/guid",
                    "text": SOURCE_GUID,
                },
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/prototype",
                    "text": "wolf-camp",
                },
            ]
        )
        self.click("spawn-editor/session", "spawn/import/prepare")
        self.wait(
            lambda: (
                self.get("spawn-editor/session", "spawn/import/status")
                .get("text", "")
                .startswith("Prepared 1 regions")
            ),
            "new object with explicit prototype",
        )
        self.click("spawn-editor/session", "spawn/import/commit")
        new_id_widget = "spawn/field/$field_k696e7374616e636573_i3_k6964"
        self.expect("spawn-host/1/1", new_id_widget, "valueText", f"tiled.{SOURCE_GUID}.2")
        self.click("spawn-host/1/1", "spawn/undo")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "circle")
        self.batch(
            [
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/prototype",
                    "text": "",
                }
            ]
        )
        self.batch(
            [
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/path",
                    "text": "data/import.tiled.json",
                },
                {
                    "op": "ui_text",
                    "host": "spawn-editor/session",
                    "widget": "spawn/import/guid",
                    "text": SOURCE_GUID,
                },
            ]
        )
        self.expect(
            "spawn-editor/session", "spawn/import/path", "valueText", "data/import.tiled.json"
        )
        self.expect("spawn-editor/session", "spawn/import/guid", "valueText", SOURCE_GUID)
        self.click("spawn-editor/session", "spawn/import/prepare")
        self.wait(
            lambda: (
                self.get("spawn-editor/session", "spawn/import/status")
                .get("text", "")
                .startswith("Prepared 1 regions")
            ),
            "prepared proposal",
        )
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "circle")
        self.capture("candidate")
        self.click("spawn-editor/session", "spawn/import/cancel")
        self.expect(
            "spawn-editor/session",
            "spawn/import/status",
            "text",
            "Import cancelled; committed source unchanged.",
        )
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "circle")
        self.click("spawn-editor/session", "spawn/import/prepare")
        self.wait(
            lambda: (
                self.get("spawn-editor/session", "spawn/import/status")
                .get("text", "")
                .startswith("Prepared 1 regions")
            ),
            "reprepared proposal",
        )
        self.click("spawn-editor/session", "spawn/import/commit")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "polygon")
        self.click("spawn-host/1/1", "spawn/undo")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "circle")
        self.click("spawn-host/1/1", "spawn/redo")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "polygon")
        self.click("spawn-host/1/2", "spawn/asset/save")
        self.expect("spawn-host/1/2", "spawn/asset/status", "text", "Configuration saved")
        self.capture("saved")
        self.click("spawn-host/1/3", "spawn/export/prepare")
        self.wait(
            lambda: (
                self.get("spawn-host/1/3", "spawn/export/status")
                .get("text", "")
                .startswith("Prepared source revision")
            ),
            "export snapshot",
        )
        self.click("spawn-host/1/3", "spawn/export/write")
        self.wait(
            lambda: (
                self.get("spawn-host/1/3", "spawn/export/written")
                .get("text", "")
                .startswith("Exported source revision")
            ),
            "export write",
        )
        saved = self.game / "Content/spawn-editor/forest.json"
        persisted = saved.read_bytes()
        (self.output / "saved-document.json").write_bytes(persisted)
        if self.save_directory is None:
            raise RuntimeError("Actual filesystem save directory was not observed")
        exported = (self.save_directory / "spawn-editor/forest.spawns.json").read_bytes()
        (self.output / "exported.spawns.json").write_bytes(exported)
        actual = json.loads(exported.decode("utf-8"))
        original = (self.game / "data/spawns.json").read_bytes()
        expected = json.loads(original.decode("utf-8"))
        expected["instances"][0]["region"] = {
            "kind": "polygon",
            "space": "world2d",
            "vertices": [[180, 160], [340, 160], [340, 280], [180, 280]],
        }
        if actual != expected:
            raise AssertionError("Runtime export differs from the one intended geometry change")
        self.click("spawn-editor/session", "spawn/import/session")
        self.stop_clean()
        self.start("reopen")
        self.expect("spawn-host/1/1", KIND_WIDGET, "valueText", "polygon")
        self.expect("spawn-host/1/2", "spawn/asset/status", "text", "Configuration saved")
        self.capture("reopened")
        if (
            saved.read_bytes() != persisted
            or (self.game / "data/spawns.json").read_bytes() != original
        ):
            raise AssertionError("Reopening rewrote persisted/source configuration")
        self.stop_clean()
        return {
            "schema": "eve.spawn-editor-ui-proof",
            "version": 1,
            "ok": True,
            "identity": self.identity,
            "document_sha256": hashlib.sha256(persisted).hexdigest(),
            "export_sha256": hashlib.sha256(exported).hexdigest(),
            "steps": [
                "new-instance-from-explicit-prototype",
                "undo-new-instance",
                "prepare-without-publication",
                "cancel",
                "reprepare",
                "commit",
                "undo",
                "redo",
                "save",
                "export",
                "clean-close",
                "reopen-saved-polygon",
                "clean-close",
            ],
            "records": self.records,
        }

    def close(self) -> None:
        if self.client:
            self.client.close()
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.log:
            self.log.close()
        (self.output / "records.json").write_text(
            json.dumps(self.records, indent=2), encoding="utf-8"
        )
        if (
            self.save_directory
            and self.save_directory.name == self.identity
            and self.save_directory.is_dir()
        ):
            shutil.rmtree(self.save_directory)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/spawn-editor-ui-evidence"))
    parser.add_argument("--port", type=int, default=7564)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "tools/scene-qc-agent"))
    from scene_qc_agent.mcp_client import EvMcpClient

    args.output.resolve().mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    scenario = EditorScenario(root, args.exe.resolve(), output, args.port, EvMcpClient)
    try:
        proof = scenario.run()
        with args.exe.resolve().open("rb") as binary:
            proof["binary_sha256"] = hashlib.file_digest(binary, "sha256").hexdigest()
        (output / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
        print(json.dumps({"ok": True, "output": str(output), "steps": proof["steps"]}))
    finally:
        scenario.close()


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
