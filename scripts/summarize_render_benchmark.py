"""Summarize frame intervals printed by examples/render-benchmark."""

import argparse
import json
import math
from pathlib import Path

import utf8_stdio


def summarize(text: str) -> dict:
    summaries = [line for line in text.splitlines() if line.startswith("BENCH_SUMMARY,")]
    if len(summaries) == 1 and "BENCH_DONE" in text:
        fields = summaries[0].split(",")
        if len(fields) != 8:
            raise ValueError("invalid BENCH_SUMMARY field count")
        n, total, median, p99, maximum, over33, over50 = map(float, fields[1:])
        if n <= 0 or total <= 0 or not all(map(math.isfinite, (n, total, median, p99, maximum))):
            raise ValueError("invalid BENCH_SUMMARY values")
        return {
            "samples": int(n),
            "average_fps": 1000.0 * n / total,
            "median_ms": median,
            "p99_ms": p99,
            "max_ms": maximum,
            "over_33ms": int(over33),
            "over_50ms": int(over50),
        }
    lines = [line for line in text.splitlines() if line.startswith("BENCH_FRAME_MS,")]
    if not lines or "BENCH_DONE" not in text:
        raise ValueError("expected frame samples and BENCH_DONE marker")
    values = [float(value) for line in lines for value in line.split(",")[1:]]
    if not values or any(not math.isfinite(value) or value <= 0 for value in values):
        raise ValueError("frame intervals must be finite and positive")
    ordered = sorted(values)

    def percentile(p: float) -> float:
        return ordered[math.ceil(p * len(ordered)) - 1]

    return {
        "samples": len(values),
        "average_fps": 1000.0 * len(values) / sum(values),
        "median_ms": percentile(0.5),
        "p99_ms": percentile(0.99),
        "max_ms": ordered[-1],
        "over_33ms": sum(value > 33.333 for value in values),
        "over_50ms": sum(value > 50.0 for value in values),
    }


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(encoding="utf-8")), indent=2))
