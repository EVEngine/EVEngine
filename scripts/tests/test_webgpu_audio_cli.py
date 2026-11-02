"""Exercise the audio gate's existing-browser and standalone entry points."""
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch
from scripts import utf8_stdio

SCRIPT = Path(__file__).resolve().parents[1] / "verify_webgpu_audio_browser.py"


class WebgpuAudioCliTests(unittest.TestCase):
    def run_gate(self, arguments, existing_page=True):
        page = MagicMock()
        page.url = "http://127.0.0.1:8091/eve.html" if existing_page else "about:blank"
        def evaluate(_function, source):
            if "getSampleCount" in source:
                return 96000
            if "getSampleRate" in source:
                return 48000
            return "true"
        page.evaluate.side_effect = evaluate
        browser = MagicMock()
        browser.contexts = [SimpleNamespace(pages=[page], new_page=MagicMock(return_value=page))]
        browser.new_page.return_value = page
        chromium = MagicMock()
        chromium.connect_over_cdp.return_value = browser
        chromium.launch.return_value = browser
        manager = MagicMock()
        manager.__enter__.return_value = SimpleNamespace(chromium=chromium)
        api = SimpleNamespace(sync_playwright=lambda: manager)
        spec = importlib.util.spec_from_file_location("audio_gate_under_test", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"playwright": SimpleNamespace(), "playwright.sync_api": api,
                                      "utf8_stdio": utf8_stdio}), \
                patch.object(sys, "argv", [str(SCRIPT), *arguments]):
            spec.loader.exec_module(module)
            self.assertEqual(module.main(), 0)
        page.wait_for_function.assert_any_call(
            "document.getElementById('log').textContent.includes('Music queued')", timeout=90000)
        page.locator.assert_called_with("#canvas")
        browser.close.assert_called_once()
        return chromium, browser, page

    def test_workflow_port_reuses_loaded_page_and_retains_audio_checks(self):
        chromium, browser, page = self.run_gate(["--port", "9223", "--timeout", "90"])
        chromium.connect_over_cdp.assert_called_once_with("http://127.0.0.1:9223")
        chromium.launch.assert_not_called()
        browser.new_page.assert_not_called()
        page.goto.assert_not_called()

    def test_missing_target_page_is_opened_in_existing_browser(self):
        _, browser, page = self.run_gate(["--port", "9223"], existing_page=False)
        browser.contexts[0].new_page.assert_called_once()
        page.goto.assert_called_once()

    def test_standalone_launch_remains_supported(self):
        chromium, _, _ = self.run_gate([])
        chromium.connect_over_cdp.assert_not_called()
        chromium.launch.assert_called_once()
