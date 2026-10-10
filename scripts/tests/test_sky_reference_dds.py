"""Exercise source-asset corruption, precision and publication boundaries."""

import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[2] / "tools" / "sky-reference"
sys.path.insert(0, str(TOOL))
from dds import TextureFormatError, decode_dds

SPEC = importlib.util.spec_from_file_location("sky_reference_export", TOOL / "export.py")
exporter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(exporter)


def dds(payload, width=1, height=1, depth=1, dxgi=28, dimension=4, mips=1, layers=1, misc=0):
    header = bytearray(148)
    header[:4] = b"DDS "
    flags = 0x1007 | (0x800000 if dimension == 4 else 0)
    struct.pack_into("<7I", header, 4, 124, flags, height, width, 0, depth, mips)
    struct.pack_into("<2I", header, 76, 32, 4)
    header[84:88] = b"DX10"
    struct.pack_into("<5I", header, 128, dxgi, dimension, misc, layers, 0)
    return bytes(header) + payload


class DdsTests(unittest.TestCase):
    def test_bgra_swizzle_preserves_each_voxel_and_alpha(self):
        source = bytes([1, 2, 3, 4, 10, 20, 30, 40])
        result = decode_dds(dds(source, depth=2, dxgi=87))
        self.assertEqual(result.pixels, bytes([3, 2, 1, 4, 30, 20, 10, 40]))
        self.assertEqual((result.width, result.height, result.depth), (1, 1, 2))

    def test_half_float_hdr_is_not_quantized_or_clamped(self):
        source = struct.pack("<4e", 16.0, 0.001, -0.25, 1.0)
        result = decode_dds(dds(source, dxgi=10))
        self.assertEqual(result.pixels, source)
        self.assertEqual(result.encoding, "rgba16f")

    def test_cube_faces_and_volume_mips_keep_exact_offsets(self):
        cube = decode_dds(dds(bytes(range(24)), dimension=3, layers=1, misc=4))
        self.assertEqual(cube.layers, 6)
        self.assertEqual(cube.subresources[5]["offset"], 20)
        volume = decode_dds(dds(bytes(36), width=2, height=2, depth=2, mips=2))
        self.assertEqual(volume.subresources[1]["offset"], 32)

    def test_float32_and_unorm16_preserve_source_precision(self):
        for dxgi, source, encoding in ((2, struct.pack("<4f", 12.5, .001, .75, 1), "rgba32f"),
                                       (11, struct.pack("<4H", 1, 32768, 65535, 42), "rgba16unorm"),
                                       (56, struct.pack("<H", 1001), "r16unorm")):
            decoded = decode_dds(dds(source, dxgi=dxgi))
            self.assertEqual(decoded.pixels, source)
            self.assertEqual(decoded.encoding, encoding)

    def test_reject_truncated_extra_compressed_and_oversized_payloads(self):
        for source in (dds(bytes(3)), dds(bytes(5)), dds(bytes(16), dxgi=98),
                       dds(bytes(4), width=0xFFFFFFFF), dds(bytes(4), layers=2)):
            with self.subTest(size=len(source)), self.assertRaises(TextureFormatError):
                decode_dds(source)
        with self.assertRaises(TextureFormatError):
            decode_dds(dds(bytes(4)), maximum_bytes=100)

    def test_reject_excessive_mips_and_bad_headers(self):
        for source in (dds(bytes(4), mips=8), b"not a DDS", dds(bytes(4), dimension=1)):
            with self.assertRaises(TextureFormatError):
                decode_dds(source)

    def test_validate_manifest_preserves_hdr_and_detects_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = dds(struct.pack("<4e", 8.0, 2.0, 0.5, 1.0), dxgi=10)
            (root / "sun.dds").write_bytes(source)
            result = {"schema": exporter.RESULT_SCHEMA, "status": "success", "engine": "test",
                      "mount": "/Game/Sky", "inventory": [], "artifacts": [
                          {"source": "/Game/Sky/Sun", "file": "sun.dds", "byteLength": len(source),
                           "sha256": hashlib.sha256(source).hexdigest(), "properties": {"srgb": False}}]}
            manifest = exporter.validate_payload(root, result)
            self.assertEqual(manifest["textures"][0]["encoding"], "rgba16f")
            self.assertEqual((root / "decoded/sun.rgba16f").read_bytes(), source[148:])
            self.assertEqual(json.loads((root / "manifest.json").read_text(encoding="utf-8"))["schema"],
                             exporter.MANIFEST_SCHEMA)
            (root / "sun.dds").write_bytes(source[:-1] + b"x")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                exporter.validate_payload(root, result)

    def test_reject_path_escape_without_writing_a_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = {"schema": exporter.RESULT_SCHEMA, "status": "success", "artifacts": [{"file": "../outside.dds"}]}
            with self.assertRaises(ValueError):
                exporter.validate_payload(root, result)
            self.assertFalse((root / "manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
