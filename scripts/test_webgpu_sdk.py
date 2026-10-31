#!/usr/bin/env python3
"""Run an extracted WebGPU SDK, requiring game startup and a rendered canvas."""

from __future__ import annotations

import argparse
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
from threading import Thread
import zipfile

import utf8_stdio
from validate_render_frame import validate


class BrowserHandler(SimpleHTTPRequestHandler):
    """Serve browser assets with the isolation needed by Emscripten threads."""

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def check_archive(archive: Path, destination: Path, version: str) -> Path:
    """Extract a self-contained browser SDK, rejecting unsafe or missing assets."""
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            if not (destination / entry.filename).resolve().is_relative_to(destination.resolve()):
                raise ValueError(f"unsafe archive entry: {entry.filename}")
        for name in ("eve.html", "eve.js", "eve.wasm", "eve.data"):
            if bundle.getinfo(f"eve-sdk/webgpu/bin/{name}").file_size == 0:
                raise ValueError(f"empty browser asset: {name}")
        bundle.extractall(destination)
    sdk = destination / "eve-sdk/webgpu"
    if (sdk / "share/eve/TARGET_PLATFORM").read_text(encoding="utf-8").strip() != "webgpu":
        raise ValueError("SDK target is not webgpu")
    actual = (sdk / "share/eve/VERSION").read_text(encoding="utf-8").strip()
    if version and actual != version:
        raise ValueError(f"SDK version mismatch: {actual} != {version}")
    return sdk / "bin"


def run(archive: Path, version: str, output: Path, *, headed: bool = False) -> None:
    """Require startup from the archive and inspect only the game canvas pixels."""
    from playwright.sync_api import sync_playwright

    output.mkdir(parents=True, exist_ok=True)
    messages: list[str] = []
    with tempfile.TemporaryDirectory() as temporary:
        root = check_archive(archive, Path(temporary), version)
        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(BrowserHandler, directory=str(root)))
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with sync_playwright() as playwright:
                browser = playwright.chromium.launch(channel="chromium", headless=not headed, args=[
                    "--no-sandbox", "--disable-gpu-sandbox", "--enable-unsafe-webgpu",
                    "--enable-features=Vulkan", "--use-vulkan=swiftshader",
                    "--enable-gpu", "--use-angle=vulkan", "--disable-vulkan-surface",
                ])
                page = browser.new_page(viewport={"width": 1280, "height": 800})
                page.add_init_script("""(() => {
                    if (typeof GPUAdapter === 'undefined') return;
                    const requestDevice = GPUAdapter.prototype.requestDevice;
                    GPUAdapter.prototype.requestDevice = async function(...args) {
                        const device = await requestDevice.apply(this, args);
                        device.lost.then(info => console.error(
                            '[sdk-webgpu-error] device lost:', info.reason, info.message));
                        device.addEventListener('uncapturederror', event => console.error(
                            '[sdk-webgpu-error] validation:', event.error.message));
                        return device;
                    };
                })();""")
                page.on("console", lambda message: messages.append(f"console {message.type}: {message.text}"))
                page.on("pageerror", lambda error: messages.append(f"pageerror: {error.stack or error}"))
                page.on("requestfailed", lambda request: messages.append(f"requestfailed: {request.url}"))
                try:
                    base = f"http://127.0.0.1:{server.server_port}"
                    page.goto(base, wait_until="domcontentloaded")
                    limits = page.evaluate("""async () => {
                        const adapter = await navigator.gpu?.requestAdapter({powerPreference: 'high-performance'});
                        if (!adapter) throw new Error('No WebGPU adapter');
                        return {
                            sampledTextures: adapter.limits.maxSampledTexturesPerShaderStage,
                            colorAttachmentBytes: adapter.limits.maxColorAttachmentBytesPerSample
                        };
                    }""")
                    (output / "adapter.json").write_text(
                        json.dumps({"browser": browser.version, "limits": limits}, indent=2), encoding="utf-8")
                    if limits["sampledTextures"] < 16 or limits["colorAttachmentBytes"] < 40:
                        raise ValueError(f"Browser adapter cannot satisfy the engine device contract: {limits}")
                    page.goto(f"{base}/eve.html", wait_until="domcontentloaded")
                    page.wait_for_function(
                        "document.querySelector('#log')?.textContent.includes('webgpu main.nut init ok')",
                        timeout=120_000,
                    )
                    # Initialization precedes the first presented frame. Poll the actual
                    # canvas, so editor text or a browser error page cannot pass the gate.
                    frame = output / "canvas.png"
                    for attempt in range(30):
                        page.locator("#canvas").screenshot(path=str(frame))
                        try:
                            validate(frame)
                            break
                        except ValueError:
                            if attempt == 29:
                                raise
                            page.wait_for_timeout(1000)
                    if any(message.startswith(("pageerror:", "requestfailed:",
                                               "console error: [sdk-webgpu-error]",
                                               "console log: [GPU_ERR]")) for message in messages):
                        raise ValueError("browser errors occurred; see browser.log")
                finally:
                    (output / "page.html").write_text(page.content(), encoding="utf-8")
                    page.screenshot(path=str(output / "page.png"))
                    browser.close()
        finally:
            (output / "browser.log").write_text("\n".join(messages), encoding="utf-8")
            server.shutdown()
            server.server_close()
            thread.join()
    print("OK: extracted WebGPU SDK initialized its game and rendered a nonblank canvas")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--version", default="")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--headed", action="store_true", help="Use a display for Chromium canvas compositing")
    args = parser.parse_args()
    run(args.archive, args.version, args.output, headed=args.headed)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
