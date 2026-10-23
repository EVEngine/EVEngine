"""Contract tests for custom lookdev shaders sharing the engine mesh vertex stage."""

from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "tools/procgen-material-lookdev/shaders"


def source(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))


def fields(text: str, pattern: str) -> str:
    match = re.search(pattern + r"\s*\{([^}]+)\}", text, re.S)
    if not match:
        raise AssertionError(f"Missing shader declaration: {pattern}")
    return re.sub(r"\s+", "", match.group(1)).replace("Light3D", "Light")


class MaterialLookdevContractTest(unittest.TestCase):
    def test_custom_frame_prefix_matches_engine_vertex_abi(self):
        vertex = source(ROOT / "src/modules/graphics/shaders/mesh3d.vert")
        expected = fields(vertex, r"uniform Frame").split("vec4ambient;")[0] + "vec4ambient;"
        for name in ("solid-sphere-glass.frag", "studio-pedestal.frag"):
            with self.subTest(shader=name):
                custom = source(SHADERS / name)
                self.assertEqual(fields(custom, r"uniform Frame"), expected)

    def test_compiled_glass_and_pedestal_keep_stable_prefix(self):
        # Use only the stable Frame prefix: light arrays differ between engine versions.
        # Perspective clip.w supplies eye depth without reading a later view matrix.
        expected = 2 * 64 + 4 * 16
        for name in ("solid-sphere-glass.frag.spv", "studio-pedestal.frag.spv"):
            with self.subTest(shader=name):
                data = (SHADERS / name).read_bytes()
                words = struct.unpack(f"<{len(data) // 4}I", data)
                members, offsets = {}, {}
                pos = 5
                while pos < len(words):
                    count, opcode = words[pos] >> 16, words[pos] & 0xFFFF
                    self.assertGreater(count, 0)
                    operands = words[pos + 1 : pos + count]
                    if opcode == 6:  # OpMemberName
                        text = struct.pack(f"<{len(operands) - 2}I", *operands[2:])
                        members[text.split(b"\0", 1)[0].decode("utf-8")] = operands[:2]
                    if opcode == 72 and operands[2] == 35:  # OpMemberDecorate Offset
                        offsets[tuple(operands[:2])] = operands[3]
                    pos += count
                self.assertEqual(offsets[tuple(members["ambient"])], expected)
