#!/usr/bin/env python3
"""Exercise the packaged WebGPU audio demo through Chrome DevTools Protocol."""

import argparse
import json
import time
from urllib.request import urlopen

import websocket

import utf8_stdio


class Browser:
    def __init__(self, port: int, deadline: float):
        self.sequence = 0
        target = None
        while time.monotonic() < deadline:
            try:
                with urlopen(f"http://127.0.0.1:{port}/json", timeout=2) as response:
                    pages = json.load(response)
                target = next((page for page in pages if page.get("type") == "page" and
                               "eve.html" in page.get("url", "")), None)
                if target:
                    break
            except (OSError, ValueError):
                pass
            time.sleep(0.5)
        if not target:
            raise RuntimeError("Chrome did not open the WebGPU page")
        self.socket = websocket.create_connection(target["webSocketDebuggerUrl"], timeout=max(1, deadline - time.monotonic()))

    def call(self, method: str, params: dict) -> dict:
        self.sequence += 1
        sequence = self.sequence
        self.socket.send(json.dumps({"id": sequence, "method": method, "params": params}))
        while True:
            message = json.loads(self.socket.recv())
            if message.get("id") != sequence:
                continue
            if "error" in message:
                raise RuntimeError(f"CDP {method}: {message['error']}")
            return message.get("result", {})

    def evaluate(self, expression: str, *, await_promise: bool = False):
        response = self.call("Runtime.evaluate", {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": await_promise,
        })
        if "exceptionDetails" in response:
            raise RuntimeError(f"Browser evaluation failed: {response['exceptionDetails']}")
        return response.get("result", {}).get("value")

    def close(self):
        self.socket.close()


def wait_for(browser: Browser, expression: str, deadline: float):
    while time.monotonic() < deadline:
        if browser.evaluate(expression):
            return
        time.sleep(0.5)
    state = browser.evaluate("""(() => ({
        ready: document.readyState,
        engine: document.getElementById('stEngine')?.textContent,
        status: document.getElementById('status')?.textContent,
        log: document.getElementById('log')?.textContent.slice(-4000),
        isolated: crossOriginIsolated
    }))()""")
    raise RuntimeError(f"Timed out waiting for browser condition: {expression}; page state: {state}")


def script_result(browser: Browser, source: str) -> str:
    expression = (
        "Module.ccall('eve_playground_eval', 'string', ['string'], "
        f"[{json.dumps(source)}], {{async: true}})"
    )
    return str(browser.evaluate(expression, await_promise=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9223)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout
    browser = Browser(args.port, deadline)
    try:
        wait_for(browser, "document.getElementById('stEngine')?.className === 'ok'", deadline)
        browser.evaluate("""(() => {
            const menu = document.getElementById('exampleSel');
            menu.value = 'playground/demos/audio.nut';
            menu.dispatchEvent(new Event('change'));
            document.getElementById('btnApply').click();
        })()""")
        wait_for(browser, "document.getElementById('log').textContent.includes('Music queued')", deadline)
        wait_for(browser, "document.getElementById('log').textContent.includes('demo applied')", deadline)
        duration = float(script_result(browser, "__pg.music.getDuration()"))
        if duration < 1.9:
            raise RuntimeError(f"Music decoded to an unexpected duration: {duration}")

        rect = browser.evaluate("""(() => {
            const r = document.getElementById('canvas').getBoundingClientRect();
            return {x: r.x + r.width / 2, y: r.y + r.height / 2};
        })()""")
        for kind in ("mousePressed", "mouseReleased"):
            browser.call("Input.dispatchMouseEvent", {
                "type": kind, "x": rect["x"], "y": rect["y"],
                "button": "left", "clickCount": 1,
            })
        if script_result(browser, "__pg.music.isLooping()") != "true":
            raise RuntimeError("Music source did not retain looping state")
        if script_result(browser, "__pg.music.isPlaying()") != "true":
            raise RuntimeError("Music source is not playing after a user gesture")
        script_result(browser, "__pg.effect.setLooping(true); __pg.effect.play()")
        if script_result(browser, "__pg.effect.isPlaying()") != "true":
            raise RuntimeError("Sound effect source did not start playback")
        print(f"browser audio OK: music duration={duration:.2f}s, music and effect playing")
        return 0
    finally:
        browser.close()


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
