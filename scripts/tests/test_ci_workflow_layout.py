"""Daily/release workflow separation and complete SDK release routing."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github/workflows"
RELEASE_ONLY = {"profile-matrix", "macos-metal-probe", "macos-x86_64", "windows-arm64", "linux-arm64",
                "dawn", "windows-release", "android-release", "macos-release",
                "ios-release", "linux-release", "example-smoke", "linux-coverage"}


def jobs(text):
    body = text.split("\njobs:\n", 1)[1]
    return set(re.findall(r"^  ([a-z0-9_-]+):$", body, re.MULTILINE))


class WorkflowLayoutTests(unittest.TestCase):
    def test_daily_graph_has_no_release_only_jobs(self):
        daily = (WORKFLOWS / "ci.yml").read_text(encoding="utf-8")
        self.assertFalse(jobs(daily) & RELEASE_ONLY)
        self.assertNotIn("inputs.build_type", daily)
        release = (WORKFLOWS / "release-ci.yml").read_text(encoding="utf-8")
        self.assertTrue(RELEASE_ONLY <= jobs(release))
        self.assertIn("uses: ./.github/workflows/ci.yml", release)
        self.assertNotIn("SCCACHE", release)
        for dependencies in re.findall(r"^    needs: \[(.*?)\]", release, re.MULTILINE):
            self.assertTrue(set(dependencies.split(", ")) <= jobs(release))

    def test_release_keeps_sdk_build_after_validation(self):
        release = (WORKFLOWS / "release.yml").read_text(encoding="utf-8")
        self.assertIn("name: Release CI\n", release)
        self.assertIn("uses: ./.github/workflows/release-ci.yml", release)
        sdk = release.split("  sdk:\n", 1)[1].split("  finish:\n", 1)[0]
        self.assertIn("needs: [start, strict-tests]", sdk)
        self.assertIn("uses: ./.github/workflows/sdk-release.yml", sdk)
        sdk_workflow = (WORKFLOWS / "sdk-release.yml").read_text(encoding="utf-8")
        self.assertIn("uses: ./.github/workflows/arm64-sdk.yml", sdk_workflow)
        self.assertIn("architecture: arm64", sdk_workflow)
        self.assertIn("architecture: x86_64", sdk_workflow)
        self.assertIn("Verify complete SDK release set", sdk_workflow)
        arm64 = (WORKFLOWS / "arm64-sdk.yml").read_text(encoding="utf-8")
        self.assertIn("platform: win32", arm64)
        self.assertIn("platform: linux", arm64)

    def test_intel_gpu_acceptance_is_serial_and_validated(self):
        release = (WORKFLOWS / "release-ci.yml").read_text(encoding="utf-8")
        intel = release.split("  macos-x86_64:\n", 1)[1].split("  windows-arm64:\n", 1)[0]
        self.assertIn('EVENGINE_VULKAN_VALIDATION: "1"', intel)
        self.assertIn("--no-tests=error", intel)
        self.assertIn("CTEST_JOBS=1 CTEST_REPEAT='' LABEL_FILTER=''", intel)
        self.assertLess(intel.index("Validate Intel macOS rendering"),
                        intel.index("Run native Intel macOS unit tests"))

    def test_shared_sdk_keeps_upstream_audio_gate_before_upload(self):
        workflow = (WORKFLOWS / "webgpu-sdk.yml").read_text(encoding="utf-8")
        self.assertIn("uses: ./.github/actions/setup-webgpu-browser", workflow)
        self.assertIn("scripts/verify_webgpu_audio_browser.py", workflow)
        self.assertIn("p.chromium.executable_path", workflow)
        self.assertLess(workflow.index("name: Verify browser audio playback"),
                        workflow.index("name: Upload verified WebGPU SDK"))

    def test_intel_probe_runs_before_the_expensive_build(self):
        workflow = (WORKFLOWS / "release-ci.yml").read_text(encoding="utf-8")
        intel = workflow.split("  macos-x86_64:\n", 1)[1].split("  windows-arm64:\n", 1)[0]
        probe = workflow.split("  macos-metal-probe:\n", 1)[1].split("  macos-x86_64:\n", 1)[0]
        self.assertIn("probe-metal-render.mm", probe)
        self.assertNotIn("needs:", probe)
        self.assertIn("needs: [debug-checks, macos-metal-probe]", intel)
        for case in ("orthoAndIsoPreview", "pure2DStack", "gltfEmbeddedUvOrientation",
                     "bindPose", "fxaaSlotReuseResizeAndPersistentCanvas",
                     "gpuSpectrumMatchesCpuAndRejectsStaleFrames"):
            self.assertIn(case, intel)
