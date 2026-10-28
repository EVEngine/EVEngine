"""Compiled provider discovery rejects ambiguous declarations before C++ linking."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_mcp_families as generator


class McpFamilyGenerationTests(unittest.TestCase):
    def test_compiled_sources_only_and_deterministic_order(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "provider.cpp"
            source.write_text('''// EVE_MCP_TOOL_FAMILY(Fake, a, b, TextValue)
const char* text = "EVE_MCP_TOOL_FAMILY(Fake, a, b, TextValue)";
EVE_MCP_TOOL_FAMILY(Zulu, schema, call, ContentEnvelope)
EVE_MCP_TOOL_FAMILY(Alpha, schema, call, TextValue)
''', encoding="utf-8")
            self.assertEqual(generator.discover([source]), ["Alpha", "Zulu"])
            self.assertIn("eveMcpToolFamily_Alpha(), eveMcpToolFamily_Zulu()", generator.render(["Alpha", "Zulu"]))
            self.assertEqual(generator.discover([]), [])

    def test_rejects_duplicate_malformed_and_unknown_encoding(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "provider.cpp"
            for text in (
                "EVE_MCP_TOOL_FAMILY(A, s, c, TextValue)\nEVE_MCP_TOOL_FAMILY(A, s, c, TextValue)",
                "EVE_MCP_TOOL_FAMILY(A, s)",
                "EVE_MCP_TOOL_FAMILY(A, s, c, Unknown)",
            ):
                with self.subTest(text=text):
                    source.write_text(text, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        generator.discover([source])
