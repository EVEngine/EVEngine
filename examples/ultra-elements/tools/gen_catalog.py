#!/usr/bin/env python3
"""Refresh fx/catalog.nut from a Humpalumps/ultra-elements checkout.

This example ships a generated catalog. Re-run the extraction used in the PR
(or ask an agent to regenerate) after pulling upstream spell param changes.

Reserved Squirrel identifiers used as param keys are rewritten (e.g. base ->
baseWidth) so the catalog parses under EveScript.
"""
from __future__ import annotations

import sys


def main() -> int:
    print(
        "catalog.nut is generated during the Ultra Elements port; "
        "clone https://github.com/Humpalumps/ultra-elements and regenerate "
        "with the agent extraction script from the PR.",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
