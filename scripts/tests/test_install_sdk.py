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
        self.assertIn('.cursor/skills/"', text)
        self.assertIn("PATTERN \"SKILL.md\"", text)
        self.assertIn("cmake/sdk/llms.txt", text)
        self.assertIn("tools/eve-mcp", text)

    def test_sdk_llms_txt_points_at_installed_paths(self) -> None:
        text = (ROOT / "cmake" / "sdk" / "llms.txt").read_text(encoding="utf-8")
        self.assertIn("share/eve/ai/SKILL.md", text)
        self.assertIn("share/eve/ai/eve-api.json", text)
        self.assertIn(".cursor/skills/README.md", text)
        self.assertIn("eve-agent-loop", text)
        self.assertIn("eve_api_search", text)
        self.assertNotIn("docs/dev/", text)

    def test_paradigm_skills_exist_with_frontmatter(self) -> None:
        skills_root = ROOT / ".cursor" / "skills"
        expected = (
            "evescript",
            "eve-agent-loop",
            "eve-game-loop",
            "eve-arpg",
            "eve-rpg",
            "eve-rts",
            "eve-action-guide",
        )
        for name in expected:
            path = skills_root / name / "SKILL.md"
            self.assertTrue(path.is_file(), msg=f"missing {path}")
            text = path.read_text(encoding="utf-8")
            self.assertTrue(text.startswith("---\n"), msg=f"{path} needs YAML frontmatter")
            self.assertIn(f"name: {name}", text)
            self.assertIn("description:", text)
        readme = (skills_root / "README.md").read_text(encoding="utf-8")
        for name in expected:
            self.assertIn(name, readme)


if __name__ == "__main__":
    unittest.main()
