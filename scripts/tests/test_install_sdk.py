"""SDK install rules must ship the agent knowledge pack with the zip."""

from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class InstallSdkAgentPackTests(unittest.TestCase):
    def test_install_sdk_ships_binding_catalog_and_skill(self) -> None:
        text = (ROOT / "cmake" / "install_sdk.cmake").read_text(encoding="utf-8")
        self.assertIn("share/eve/ai", text)
        self.assertIn("eve-api.json", text)
        self.assertIn("eve-api.d.ts", text)
        self.assertIn(".cursor/skills/evescript", text)
        self.assertIn("cmake/sdk/llms.txt", text)
        self.assertIn("tools/eve-mcp", text)

    def test_sdk_llms_txt_points_at_installed_paths(self) -> None:
        text = (ROOT / "cmake" / "sdk" / "llms.txt").read_text(encoding="utf-8")
        self.assertIn("share/eve/ai/SKILL.md", text)
        self.assertIn("share/eve/ai/eve-api.json", text)
        self.assertIn("eve_api_search", text)
        self.assertNotIn("docs/dev/", text)


if __name__ == "__main__":
    unittest.main()
