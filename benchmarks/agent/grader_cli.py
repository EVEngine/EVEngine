#!/usr/bin/env python3
"""Run trusted acceptance checks outside the adapter process and its time budget."""

from pathlib import Path
import sys

from common import read_json, write_json
from grade import contract_grade, engine_grade


def main():
    request = read_json(sys.argv[1])
    project = Path(request["project"])
    if request["grader"] == "contract":
        checks = contract_grade(project, request["originalContract"])
    else:
        checks = engine_grade(
            project,
            request["category"],
            request["seed"],
            Path(request["engine"]),
            Path(request["artifacts"]),
        )
    write_json(sys.argv[2], {"schema": "eve.agent-benchmark-grade/v1", "checks": checks})


if __name__ == "__main__":
    raise SystemExit(main())
