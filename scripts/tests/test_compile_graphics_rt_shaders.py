"""Failure-path contracts for publishing one coherent RT shader family."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compile_graphics_rt_shaders as compiler


class RayTracingShaderBuildTests(unittest.TestCase):
    def test_compiled_path_shader_matches_host_upload_abi(self):
        self.assert_shader_layout("pt_path_rgen_spv.inc", {
            "PtVertex": ([0, 16, 32, 48, 64], 80),
            "PtTriangle": ([0, 16], 32),
            "PtInstance": ([0, 64, 128], 144),
            "PtMaterial": ([0, 16, 32, 48], 64),
            "PtTexture": ([0, 16, 32], 48),
            "PtLight": ([0, 16, 32, 48], 64),
            "Constants": ([0, 64, 80, 96, 112], None),
        })

    def test_compiled_temporal_push_constants_match_host_layout(self):
        self.assert_shader_layout("pt_temporal_spv.inc", {"TemporalConstants": ([0, 64, 80, 96, 112], None)})
        self.assert_shader_layout("pt_denoise_spv.inc", {"DenoiseSettings": ([112], None)})

    def assert_shader_layout(self, filename, expected):
        # Inspect actual compiled SPIR-V, rather than assuming GLSL member layout.
        source = (compiler.SHADER_DIR / filename).read_text(encoding="utf-8")
        words = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]{8}", source)]
        names, offsets, strides, arrays, column_major = {}, {}, {}, {}, set()
        cursor = 5
        while cursor < len(words):
            length, opcode = words[cursor] >> 16, words[cursor] & 0xffff
            self.assertGreater(length, 0)
            operands = words[cursor + 1:cursor + length]
            self.assertEqual(len(operands), length - 1)
            if opcode == 5:  # OpName
                encoded = struct.pack(f"<{len(operands) - 1}I", *operands[1:])
                names[operands[0]] = encoded.split(b"\0", 1)[0].decode("utf-8")
            elif opcode == 29:  # OpTypeRuntimeArray
                arrays[operands[0]] = operands[1]
            elif opcode == 71 and operands[1] == 6:  # ArrayStride
                strides[operands[0]] = operands[2]
            elif opcode == 72:  # OpMemberDecorate
                key = (operands[0], operands[1])
                if operands[2] == 35:  # Offset
                    offsets[key] = operands[3]
                elif operands[2] == 5:  # ColMajor
                    column_major.add(key)
                elif operands[2] == 7:  # MatrixStride
                    self.assertEqual(operands[3], 16)
            cursor += length
        self.assertEqual(cursor, len(words))
        for name, (member_offsets, stride) in expected.items():
            # GLSL also emits undecorated function-local copies of these types.
            matches = [type_id for type_id, label in names.items()
                       if label == name and (type_id, 0) in offsets]
            self.assertEqual(len(matches), 1, name)
            type_id = matches[0]
            self.assertEqual([offsets[(type_id, i)] for i in range(len(member_offsets))], member_offsets, name)
            if stride is not None:
                self.assertIn(stride, [strides[array] for array, element in arrays.items() if element == type_id], name)
            for member in range(2 if name == "PtInstance" else int(name in ("Constants", "TemporalConstants"))):
                self.assertIn((type_id, member), column_major, name)

    def test_invalid_bytecode_does_not_overwrite_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / "bad.spv", root / "old.inc"
            target.write_text("previous shader", encoding="utf-8")
            for invalid in (b"abc", struct.pack("<I", 123)):
                source.write_bytes(invalid)
                with self.assertRaises(SystemExit):
                    compiler.spv_to_inc(source, "bad", target)
                self.assertEqual(target.read_text(encoding="utf-8"), "previous shader")

    def test_late_validation_failure_preserves_entire_family(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("first.rgen", "second.rmiss", "path_sampling.comp"):
                (root / name).write_text("shader source", encoding="utf-8")
            target = root / "first_spv.inc"
            target.write_text("last known good", encoding="utf-8")

            def execute(command, **kwargs):
                self.assertEqual(kwargs["encoding"], "utf-8")
                if command[0] == "glslc":
                    Path(command[-1]).write_bytes(struct.pack("<I", 0x07230203))
                invalid = command[0] == "spirv-val" and "second_spv" in command[-1]
                return subprocess.CompletedProcess(command, int(invalid), "", "invalid shader" if invalid else "")

            with patch.object(compiler, "SHADER_DIR", root), \
                 patch.object(compiler, "SAMPLING_TEST", root), \
                 patch.object(compiler, "SHADERS", [("first.rgen", "rgen", "first_spv"),
                                                    ("second.rmiss", "rmiss", "second_spv")]), \
                 patch.object(compiler.shutil, "which", side_effect=lambda name: name), \
                 patch.object(compiler.subprocess, "run", side_effect=execute):
                with self.assertRaisesRegex(SystemExit, "invalid shader"):
                    compiler.main()
            self.assertEqual(target.read_text(encoding="utf-8"), "last known good")
            self.assertFalse((root / "second_spv.inc").exists())

    def test_array_preserves_little_endian_words(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / "valid.spv", root / "valid.inc"
            source.write_bytes(struct.pack("<3I", 0x07230203, 0x12345678, 0xffffffff))
            with patch.object(compiler, "ROOT", root), contextlib.redirect_stdout(io.StringIO()):
                compiler.spv_to_inc(source, "probe", target)
            output = target.read_text(encoding="utf-8")
            self.assertIn("0x07230203, 0x12345678, 0xffffffff", output)
            self.assertIn("probe_count = 3", output)
