"""Guard against new hand-counted `sq_call` sites.

`sq_call(vm, params, ...)` takes the callee at `-(params)-1`, so `params` counts the
receiver slot plus the arguments. Writing that number by hand produced five separate
defects in this repository, all silent:

  * Scene::callCallback handed the callback `index` where it expected an entity;
  * Scene::callEventCallback, Dialogue::evalScriptPredicate and DialogueFlow::evaluate
    each passed a count one short, so the VM's arity check rejected the call and the
    callback never ran at all.

`eve::script::callFunction` / `callFunctionResult` (common/SquirrelBinding.h) derive the
count from the pushes, so it cannot disagree with them. This checker keeps the
remaining hand-written sites at their reviewed baseline (each was checked by hand, see
plan doc 9.22.2) and fails when a new one appears, so new code goes through the helper.

The key is file plus the whitespace-normalized line, so unrelated edits that shift line
numbers do not disturb the baseline while a rewritten call does get re-reviewed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import utf8_stdio

BASELINE = Path("scripts/sq_call_sites_baseline.json")
SOURCE_ROOT = Path("src")
CALL = re.compile(r"\bsq_call\s*\(")


def normalize(line: str) -> str:
    return re.sub(r"\s+", " ", line.strip())


def scan(root: Path) -> list[str]:
    """Return sorted `relative/path|normalized line|occurrence` keys for every site.

    The occurrence index matters: two sites in one file can normalize to the same text
    (e.g. the same `if (SQ_FAILED(sq_call(vm, 1, ...)))` guard twice), and without it a
    newly added copy would be indistinguishable from the reviewed one.
    """
    keys: list[str] = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        seen: dict[str, int] = {}
        for line in text.splitlines():
            if line.lstrip().startswith("//"):
                continue
            if not CALL.search(line):
                continue
            body = normalize(line)
            seen[body] = seen.get(body, 0) + 1
            keys.append(f"{path.as_posix()}|{body}|{seen[body]}")
    return sorted(keys)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-baseline", action="store_true", help="record the current sites")
    parser.add_argument("--root", type=Path, default=SOURCE_ROOT)
    args = parser.parse_args()

    found = scan(args.root)
    if args.write_baseline:
        BASELINE.write_text(
            json.dumps({"schema_version": 1, "sites": found}, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"sq_call sites: wrote {len(found)} entries to {BASELINE}")
        return 0

    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))["sites"]
    known = set(baseline)
    current = set(found)
    added = sorted(current - known)
    stale = sorted(known - current)

    for key in added:
        print(f"NEW hand-counted sq_call (use eve::script::callFunction): {key}")
    for key in stale:
        print(f"stale baseline entry (site changed or removed): {key}")
    if added or stale:
        print(
            f"sq_call site check: {len(added)} new, {len(stale)} stale, "
            f"{len(known)} reviewed baseline entries"
        )
        return 1
    print(f"sq_call site check OK: {len(known)} reviewed hand-counted sites, no new ones")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
