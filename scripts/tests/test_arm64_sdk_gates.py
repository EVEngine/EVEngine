"""Failure-injection coverage for ARM64 SDK architecture and runtime gates."""

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / ".github/scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VERIFY = load("verify-arm64-sdk")
RUN = load("run-sdk-game")


class Arm64SdkArchitectureTests(unittest.TestCase):
    def test_pe_rejects_x64_and_accepts_arm64(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "runtime.dll"
            for machine in (0x8664, 0xAA64):
                content = bytearray(70)
                content[:2] = b"MZ"
                struct.pack_into("<I", content, 60, 64)
                content[64:68] = b"PE\0\0"
                struct.pack_into("<H", content, 68, machine)
                binary.write_bytes(content)
                if machine == 0xAA64:
                    VERIFY.verify(binary)
                else:
                    with self.assertRaises(ValueError):
                        VERIFY.verify(binary)

    def test_elf_rejects_x64_and_accepts_aarch64(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "eve"
            for machine in (62, 183):
                content = bytearray(64)
                content[:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<H", content, 18, machine)
                binary.write_bytes(content)
                if machine == 183:
                    VERIFY.verify(binary)
                else:
                    with self.assertRaises(ValueError):
                        VERIFY.verify(binary)

    def test_truncated_or_unknown_binary_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "eve"
            for content in (b"MZ", b"\x7fELF", b"", b"not a binary"):
                binary.write_bytes(content)
                with self.assertRaises(ValueError):
                    VERIFY.verify(binary)


class Arm64SdkRuntimeTests(unittest.TestCase):
    def run_program(self, source):
        RUN.check_game([sys.executable, "-u", "-c", source], ROOT, "GAME_OK", 0.2, 0.8)

    def test_marker_and_liveness_pass(self):
        self.run_program("import time; print('GAME_OK'); time.sleep(10)")

    def test_early_exit_even_with_marker_fails(self):
        with self.assertRaisesRegex(RuntimeError, "exited early"):
            self.run_program("print('GAME_OK')")

    def test_live_process_without_marker_fails(self):
        with self.assertRaisesRegex(RuntimeError, "marker missing"):
            self.run_program("import time; time.sleep(10)")
