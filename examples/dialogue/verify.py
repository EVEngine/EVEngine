"""Verify the canonical dnut dialogue flow through a live engine MCP session."""

import argparse
import json
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--port", type=int, default=7541)
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "verification")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def call(name, arguments):
        with socket.create_connection(("127.0.0.1", args.port), timeout=10) as stream:
            request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": arguments}}
            stream.sendall((json.dumps(request) + "\n").encode())
            with stream.makefile("rb") as reader:
                for line in reader:
                    response = json.loads(line)
                    if response.get("id") != 1:
                        continue
                    if "error" in response:
                        raise RuntimeError(response["error"])
                    result = response["result"]
                    text = "\n".join(item.get("text", "") for item in result.get("content", []))
                    if result.get("isError") or text.startswith("error:"):
                        raise RuntimeError(text)
                    return text
        raise RuntimeError("MCP closed without a response")

    def script(source):
        call("eve_run_script", {"source": source})
        time.sleep(0.25)

    def hide_panels():
        script('if(eve.dev.ai.isVisible())eve.dev.ai.toggleVisible();if(eve.dev.console.isVisible())eve.dev.console.toggleVisible();')

    def capture(name):
        path = output / f"{name}.png"
        for attempt in range(10):
            try:
                call("eve_screenshot", {"path": path.as_posix()})
                break
            except RuntimeError as error:
                if "no presented frame" not in str(error) or attempt == 9:
                    raise
                time.sleep(0.15)
        if not path.exists() or not path.stat().st_size:
            raise RuntimeError(f"empty screenshot: {path}")
        print(path)

    time.sleep(1.0)
    hide_panels()
    script('if(dialogueFlow.getNodeKind()!="line")throw "expected opening line";')
    hide_panels()
    capture("line")
    script('dlg.skipTyping();')
    script('tryAdvance();')
    script('if(dialogueFlow.getNodeKind()!="choice")throw "expected choice, got "+dialogueFlow.getNodeKind()+"/"+dialogueFlow.getNodeId();')
    hide_panels()
    capture("choice")
    script('tryChoice(0);if(dialogueFlow.getNodeKind()=="choice")throw "choice input did not select";')
    script('dlg.skipTyping();')
    script('tryAdvance();')
    script('tryAdvance();')
    time.sleep(1.2)
    script('if(!restoredCommandOnce || dialogueFlow.getNodeKind()!="line")throw "pending command restore did not resume: "+restoredCommandOnce+"/"+dialogueFlow.getNodeKind()+"/"+dialogueFlow.getNodeId()+"/"+dialogueFlow.getPendingCommandRequestId();')
    hide_panels()
    capture("restored-command")
    print("PASS: line, choice, pending-command save/restore/resume")


if __name__ == "__main__":
    main()
