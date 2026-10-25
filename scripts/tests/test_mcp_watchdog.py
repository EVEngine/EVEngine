"""Native SDK MCP supervision contract. Python is only the repository test driver."""

from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import utf8_stdio  # noqa: E402


def call(ident, name, source=None):
    return {
        "jsonrpc": "2.0",
        "id": ident,
        "method": "tools/call",
        "params": {"name": name, "arguments": {} if source is None else {"source": source}},
    }


def text(response):
    return response["result"]["content"][0]["text"]


@unittest.skipUnless(os.environ.get("EVE_MCP_TEST_BIN"), "requires rebuilt or installed native eve")
class NativeSupervisorTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory(prefix="eve SDK test ")
        self.addCleanup(self.root.cleanup)
        env = dict(os.environ)
        env["PATH"] = ""  # No Python, node, shell helper or repository script is discoverable.
        self.process = subprocess.Popen(
            [
                os.environ["EVE_MCP_TEST_BIN"],
                "mcp",
                "--timeout-ms",
                "2000",
                "--root",
                self.root.name,
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            env=env,
            cwd=self.root.name,
        )
        self.responses = queue.Queue()

        def read():
            for line in self.process.stdout:
                self.responses.put(json.loads(line))
            self.responses.put(None)

        threading.Thread(target=read, daemon=True).start()
        self.addCleanup(self.stop)
        initialized = self.request(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-06-18",
                    "capabilities": {},
                    "clientInfo": {"name": "native-sdk-test", "version": "1"},
                },
            }
        )
        self.assertIn("result", initialized)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        self.process.stdout.close()

    def request(self, request):
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        while True:
            response = self.responses.get(timeout=10)
            self.assertIsNotNone(response, "native supervisor disconnected")
            if response.get("id") == request["id"]:
                return response

    def worker_pid(self):
        children = Path(f"/proc/{self.process.pid}/task/{self.process.pid}/children")
        if not children.exists():
            return None
        pids = children.read_text(encoding="utf-8").split()
        self.assertEqual(len(pids), 1)
        return int(pids[0])

    def assert_worker_reaped(self, pid):
        if pid is not None:
            self.assertFalse(Path(f"/proc/{pid}").exists(), "old VM process survived")

    def test_timeout_long_task_repeated_recovery_and_error_history(self):
        self.assertEqual(text(self.request(call(2, "eve_error_slice"))), "no prior error")
        supervisor_pid = self.process.pid
        for ident in (3, 6):
            worker = self.worker_pid()
            started = time.monotonic()
            timeout = self.request(
                call(ident, "eve_run_script", "lostState <- 123; while (true) {}")
            )
            self.assertTrue(timeout["result"]["isError"])
            diagnostic = json.loads(text(timeout))
            self.assertEqual(diagnostic["type"], "execution_timeout")
            self.assertEqual(diagnostic["vm_state"], "terminated")
            self.assert_worker_reaped(worker)
            self.assertEqual(diagnostic["budget_ms"], 2000)
            self.assertLess(time.monotonic() - started, 5)
            self.assertEqual(text(self.request(call(ident + 1, "eve_error_slice"))), text(timeout))
            normal = self.request(
                call(
                    ident + 2,
                    "eve_run_script",
                    'if ("lostState" in getroottable()) throw "old VM reused"; '
                    "local sum=0; for(local i=0;i<1000000;i++) sum += i;",
                )
            )
            self.assertEqual(text(normal), "ok")
            self.assertEqual(self.process.pid, supervisor_pid)
        for ident, source, kind, message in [
            (10, 'throw "runtime-marker";', "runtime_error", "runtime-marker"),
            (14, "auditInvalid <- ;", "compile_error", "expression expected"),
            (18, "local broken = ;", "compile_error", "expression expected"),
            (22, 'throw "new-runtime";', "runtime_error", "new-runtime"),
        ]:
            failure = self.request(call(ident, "eve_run_script", source))
            self.assertTrue(failure["result"]["isError"])
            report = text(self.request(call(ident + 1, "eve_error_slice")))
            self.assertIn(kind, report)
            self.assertIn(message, report)
            self.assertIn("memory://mcp/snippet", report)
            self.assertIn("line: 1", report)
            if kind == "compile_error":
                self.assertNotIn("Stack:", report)
                self.assertNotIn("runtime-marker", report)
                self.assertIn("column:", report)
            self.assertEqual(
                text(self.request(call(ident + 2, "eve_run_script", "local fixed=1;"))), "ok"
            )
            self.assertEqual(text(self.request(call(ident + 3, "eve_error_slice"))), report)

    def test_client_eof_exits_supervisor(self):
        worker = self.worker_pid()
        self.process.stdin.close()
        self.assertEqual(self.process.wait(timeout=5), 0)
        self.assert_worker_reaped(worker)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux process lifetime check")
    def test_client_termination_reaps_worker(self):
        worker = self.worker_pid()
        self.process.terminate()
        self.process.wait(timeout=5)
        self.assert_worker_reaped(worker)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
