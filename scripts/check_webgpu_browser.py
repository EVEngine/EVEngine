#!/usr/bin/env python3
"""Exercise rendered WebGPU canvases in the browser playground."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from PIL import Image

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
            browser_errors = []
            first_gpu_messages = []
            page.on("pageerror", lambda error: browser_errors.append(str(error)))
            def record_console(message):
                if message.type == "error":
                    browser_errors.append(message.text)
                if message.text.startswith("[GPU_ERR]") and len(first_gpu_messages) < 30:
                    first_gpu_messages.append(message.text)

            page.on("console", record_console)
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

            failures = []
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
                    early_logs = page.evaluate("window.webgpuEarlyLogs || []")
                    relevant_early_logs = [
                        line for line in early_logs
                        if "[GPU_ERR]" in line or "[webgpu]" in line or "error" in line.lower()
                    ][:40]
                    failures.append(
                        f"{name}: {error}; status={status!r}; browser errors={browser_errors[-10:]!r}; "
                        f"first GPU messages={first_gpu_messages!r}; "
                        f"early logs={relevant_early_logs!r}; "
                        f"page log:\n{recent_log}"
                    )
                else:
                    print(f"{name}: canvas OK, mean={mean:.3f}, stddev={deviation:.3f}")
                page.locator("#btnShot").click()
                try:
                    page.wait_for_function(
                        "document.getElementById('shotOverlay').style.display === 'flex' && "
                        "document.getElementById('shotImg').naturalWidth > 0",
                        timeout=12000,
                    )
                    engine_capture = args.output / f"{name}_engine.png"
                    page.locator("#shotImg").screenshot(path=str(engine_capture))
                    engine_mean, engine_deviation = validate(engine_capture)
                    print(f"{name}: engine readback OK, mean={engine_mean:.3f}, stddev={engine_deviation:.3f}")
                except Exception as error:
                    failures.append(f"{name}: engine readback failed: {error}; page log: "
                                    f"{page.locator('#log').inner_text()[-1500:]}")
                finally:
                    page.locator("#shotOverlay").evaluate("element => element.style.display = 'none'")
            if failures:
                try:
                    page.evaluate("""async () => {
                        const canvas = document.createElement('canvas');
                        canvas.id = 'webgpu-probe';
                        canvas.width = canvas.height = 64;
                        canvas.style.cssText = 'position:fixed;top:0;left:0;z-index:1000;width:64px;height:64px';
                        document.body.appendChild(canvas);
                        const adapter = await navigator.gpu.requestAdapter();
                        const device = await adapter.requestDevice();
                        const context = canvas.getContext('webgpu');
                        context.configure({device, format: navigator.gpu.getPreferredCanvasFormat(), alphaMode: 'opaque'});
                        const draw = () => {
                            const encoder = device.createCommandEncoder();
                            const pass = encoder.beginRenderPass({colorAttachments: [{
                                view: context.getCurrentTexture().createView(),
                                loadOp: 'clear', storeOp: 'store',
                                clearValue: {r: 1, g: 0, b: 1, a: 1}
                            }]});
                            pass.end();
                            device.queue.submit([encoder.finish()]);
                            requestAnimationFrame(draw);
                        };
                        requestAnimationFrame(draw);
                        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
                    }""")
                    probe_path = args.output / "webgpu_probe.png"
                    page.locator("#webgpu-probe").screenshot(path=str(probe_path))
                    with Image.open(probe_path) as probe_image:
                        probe_result = probe_image.convert("RGB").getpixel((32, 32))
                except Exception as error:
                    probe_result = f"failed: {error}"
                raise RuntimeError(f"browser WebGPU probe={probe_result!r}\n\n" + "\n\n".join(failures))
        finally:
            browser.close()
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
