"""Asset FetchContent helpers must retry transient download failures."""

from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class AssetDependencyDownloadTests(unittest.TestCase):
    def test_zstd_and_utf8proc_use_retry_prefetch(self) -> None:
        text = (ROOT / "cmake" / "asset_dependencies.cmake").read_text(encoding="utf-8")
        self.assertIn("eve_download_archive_with_retries", text)
        self.assertIn("_eve_downloads/zstd-1.5.7.tar.gz", text)
        self.assertIn("_eve_downloads/utf8proc-2.11.3.tar.gz", text)
        self.assertIn("Failed to download", text)
        self.assertIn("_eve_max_attempts 5", text)
        # Prefetch local archives; do not hit GitHub releases once per URL only.
        self.assertIn('URL "${_eve_zstd_archive}"', text)
        self.assertIn('URL "${_eve_utf8proc_archive}"', text)


class VulkanSdkRetryActionTests(unittest.TestCase):
    def test_windows_swiftshader_jobs_use_retry_action(self) -> None:
        action = ROOT / ".github" / "actions" / "install-vulkan-sdk" / "action.yml"
        self.assertTrue(action.is_file())
        text = action.read_text(encoding="utf-8")
        self.assertIn("attempt 1/3", text)
        self.assertIn("attempt 3/3", text)
        self.assertIn("jakoch/install-vulkan-sdk-action@", text)

        for rel in (
            ".github/workflows/ci.yml",
            ".github/workflows/release-ci.yml",
            ".github/workflows/sdk-release.yml",
        ):
            wf = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("install_swiftshader: true", wf)
            # Every SwiftShader install must go through the retry wrapper.
            blocks = wf.split("install_swiftshader: true")
            # Each occurrence (except trailing split) should be preceded by the wrapper.
            for prefix in blocks[:-1]:
                self.assertIn(
                    "uses: ./.github/actions/install-vulkan-sdk",
                    prefix[-400:],
                    msg=f"{rel} SwiftShader install missing retry wrapper",
                )


if __name__ == "__main__":
    unittest.main()
