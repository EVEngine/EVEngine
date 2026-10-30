#!/usr/bin/env python3
"""Exercise rendered WebGPU canvases in the browser playground."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

import utf8_stdio
from validate_render_frame import validate


SCENES = (
    ("3d_sphere", None, "webgpu main.nut init ok"),
    ("2d_sprites", "playground/demos/2d_sprites.nut", "2d sprites demo init ok"),
    ("3d_textured", "playground/demos/3d_textured.nut", "3d textured demo init ok"),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8090/eve.html")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--browser", type=Path, help="Chrome, Chromium, or Edge executable")
    args = parser.parse_args()
    chrome = args.browser or next(
        (
            executable
            for name in ("google-chrome", "chromium", "msedge")
            if (executable := shutil.which(name))
        ),
        None,
    )
    if chrome is None:
        raise RuntimeError("Chrome, Chromium, or Edge is required for the browser smoke test")

    from playwright.sync_api import TimeoutError as PlaywrightTimeoutError, sync_playwright

    args.output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            executable_path=str(chrome),
            headless=True,
            args=[
                "--no-sandbox",
                "--disable-gpu-sandbox",
                "--enable-unsafe-webgpu",
                "--enable-features=Vulkan",
                "--use-vulkan=swiftshader",
            ],
        )
        try:
            page = browser.new_page(viewport={"width": 1200, "height": 800})
            page.goto(args.url, wait_until="domcontentloaded", timeout=30000)
            page.wait_for_function(
                "document.getElementById('stEngine').textContent.includes('已就绪')",
                timeout=90000,
            )
            isolated = page.evaluate("crossOriginIsolated")
            if not isolated:
                raise RuntimeError("browser page lacks COOP/COEP cross-origin isolation")
            page.wait_for_function(
                "document.getElementById('stAdapter').className === 'ok'",
                timeout=10000,
            )
            adapter = page.locator("#stAdapter").inner_text()
            print(f"browser ready: {adapter}")

            for name, demo, initialized in SCENES:
                if demo is None:
                    page.locator("#btnRestart").click()
                else:
                    page.locator("#exampleSel").select_option(demo)
                    page.locator("#btnApply").click()
                try:
                    page.wait_for_function(
                        "marker => document.getElementById('log').textContent.includes(marker)",
                        arg=initialized,
                        timeout=10000,
                    )
                except PlaywrightTimeoutError as error:
                    recent_log = page.locator("#log").inner_text()[-4000:]
                    raise RuntimeError(f"{name}: missing init marker {initialized!r}; page log:\n{recent_log}") from error
                page.wait_for_timeout(1500)
                capture = args.output / f"{name}.png"
                page.locator("#canvas").screenshot(path=str(capture), timeout=15000)
                try:
                    mean, deviation = validate(capture)
                except ValueError as error:
                    recent_log = page.locator("#log").inner_text()[-4000:]
                    status = page.locator("#status").inner_text()
                    raise RuntimeError(f"{name}: {error}; status={status!r}; page log:\n{recent_log}") from error
                print(f"{name}: canvas OK, mean={mean:.3f}, stddev={deviation:.3f}")
        finally:
            browser.close()
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
