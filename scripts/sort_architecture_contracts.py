#!/usr/bin/env python3
"""Retired: architecture contract JSON shards are no longer maintained.

The code-first gate lives in ``check_architecture_contracts.py`` and reads
source / a hot-path list in that module. See
``docs/dev/superpowers/specs/2026-10-14-architecture-contracts-code-first.md``.
"""

from __future__ import annotations

import sys

import utf8_stdio


def main() -> int:
    print(
        "sort_architecture_contracts.py: retired — "
        "scripts/architecture_contracts*.json were removed; "
        "use scripts/check_architecture_contracts.py",
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
