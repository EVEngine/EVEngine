#!/usr/bin/env python3
"""Smoke-check tools/eve-mcp/server.js lifecycle helpers without spawning eve."""

from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import utf8_stdio  # noqa: E402

SERVER = ROOT / "tools" / "eve-mcp" / "server.js"


class EveMcpBridgeTests(unittest.TestCase):
    def test_server_documents_auto_start_and_filters(self) -> None:
        text = SERVER.read_text(encoding="utf-8")
        self.assertIn("EVE_MCP_AUTO_START", text)
        self.assertIn("EVE_MCP_EVE_BIN", text)
        self.assertIn("EVE_MCP_GAME", text)
        self.assertIn("EVE_MCP_TOOLSET", text)
        self.assertIn("spawn(", text)
        self.assertIn("SIGTERM", text)
        # Keep the bridge dependency-free.
        self.assertNotIn("require('ws')", text)
        self.assertNotIn("require(\"ws\")", text)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
