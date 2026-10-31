#!/usr/bin/env python3
"""Exercise the packaged WebGPU audio demo in a real Chromium page."""

import argparse

from playwright.sync_api import sync_playwright

import utf8_stdio


def script_result(page, source: str) -> str:
    return str(page.evaluate(
        "source => Module.ccall('eve_playground_eval', 'string', ['string'], "
        "[source], {async: true})", source))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8091/eve.html")
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=False, args=[
            "--no-sandbox", "--disable-gpu-sandbox", "--enable-unsafe-webgpu",
            "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist", "--enable-gpu",
            "--use-gpu-in-tests", "--enable-accelerated-2d-canvas",
            "--use-angle=vulkan", "--enable-features=Vulkan", "--disable-vulkan-surface",
            "--use-vulkan=swiftshader", "--use-webgpu-adapter=swiftshader",
        ])
        try:
            page = browser.new_page(viewport={"width": 1400, "height": 900})
            page.goto(args.url, wait_until="domcontentloaded", timeout=30000)
            timeout_ms = args.timeout * 1000
            page.wait_for_function("document.getElementById('stEngine')?.className === 'ok'",
                                   timeout=timeout_ms)
            page.wait_for_function("document.getElementById('log').textContent.includes('Music queued')",
                                   timeout=timeout_ms)
            samples = int(script_result(page, "__pg.musicData.getSampleCount()"))
            rate = int(script_result(page, "__pg.musicData.getSampleRate()"))
            duration = samples / rate
            if duration < 1.9:
                raise RuntimeError(f"Music decoded to an unexpected duration: {duration}")

            page.locator("#canvas").click()
            if script_result(page, "__pg.music.isLooping()") != "true":
                raise RuntimeError("Music source did not retain looping state")
            if script_result(page, "__pg.music.isPlaying()") != "true":
                raise RuntimeError("Music source is not playing after a user gesture")
            script_result(page, "__pg.effect.setLooping(true); __pg.effect.play()")
            if script_result(page, "__pg.effect.isPlaying()") != "true":
                raise RuntimeError("Sound effect source did not start playback")
            print(f"browser audio OK: music duration={duration:.2f}s, music and effect playing")
            return 0
        finally:
            browser.close()


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
