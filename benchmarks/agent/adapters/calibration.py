#!/usr/bin/env python3
"""Known broken/no-op and known fixed controls, never model evaluation results."""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import read_json, write_json  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("noop", "reference"))
    parser.add_argument("request", type=Path)
    parser.add_argument("result", type=Path)
    args = parser.parse_args()
    request = read_json(args.request)
    if args.mode == "reference":
        suite = read_json(Path(__file__).resolve().parents[1] / "suite.json")
        task = next(t for t in suite["tasks"] if t["id"] == request["taskId"])
        mutation = task["mutation"]
        path = Path(request["workspace"]) / mutation["file"]
        source = path.read_text(encoding="utf-8")
        replacement = "attack = 20.0" if task["category"] == "balance" else mutation["old"]
        if source.count(mutation["new"]) != 1:
            raise ValueError("control fixture drift")
        path.write_text(source.replace(mutation["new"], replacement), encoding="utf-8")
    write_json(args.result, {"schema": "eve.agent-benchmark-adapter-result/v1", "usage": {}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
