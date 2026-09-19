#!/usr/bin/env python3
"""Classify test/*.cpp sources into the unit_test_<domain> link units.

This is the single implementation of the partition rule. It is used twice:

* the configure step (test/CMakeLists.txt) runs `--emit-cmake` so CMake consumes
  the partition instead of re-implementing it, and
* scripts/check_test_manifest.py imports `partition`/`table_contract_errors` so a
  test that matches no rule -- or two rules -- fails without configuring.

The rule itself, and the table it reads, are documented in test/test_domains.cmake.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "test"
DOMAIN_TABLE = TEST_DIR / "test_domains.cmake"
MODULES_DIR = REPO / "src" / "modules"

TABLE_ROW_RE = re.compile(r'"([^";]+);([a-z0-9_]+)"')
MODULE_INCLUDE_RE = re.compile(r'#include\s+"([A-Za-z0-9_/.]+)"')
DOMAIN_NAME_RE = re.compile(r"^\s*([a-z0-9_]+)\s*$", re.MULTILINE)

# A test translation unit that provides the runner entry point for every domain
# executable rather than testing one domain.
SHARED_RUNNER = "main"


def _table_body(text: str, name: str) -> str:
    """Return the text of `set(<name> ... )`, up to its CACHE INTERNAL trailer."""
    match = re.search(
        rf"set\(\s*{re.escape(name)}\b(.*?)\n\s*CACHE INTERNAL",
        text,
        re.DOTALL,
    )
    return match.group(1) if match else ""


def parse_domain_table(text: str) -> dict[str, object]:
    """Read the domain table from test/test_domains.cmake."""
    domains = DOMAIN_NAME_RE.findall(_table_body(text, "EVE_TEST_DOMAINS"))
    module_domain = dict(TABLE_ROW_RE.findall(_table_body(text, "EVE_TEST_MODULE_DOMAIN")))
    package_override = dict(
        TABLE_ROW_RE.findall(_table_body(text, "EVE_TEST_PACKAGE_OVERRIDE"))
    )
    prefix_rules = TABLE_ROW_RE.findall(_table_body(text, "EVE_TEST_PREFIX_DOMAIN"))
    return {
        "domains": domains,
        "module_domain": module_domain,
        "package_override": package_override,
        "prefix_rules": prefix_rules,
    }


def table_contract_errors(tables: dict[str, object]) -> list[str]:
    """Return errors for a malformed or self-inconsistent domain table."""
    errors: list[str] = []
    domains = list(tables["domains"])  # type: ignore[arg-type]
    if not domains:
        return ["test_domains.cmake must declare EVE_TEST_DOMAINS"]

    known = set(domains)
    for key in ("module_domain", "package_override"):
        for row_key, domain in dict(tables[key]).items():  # type: ignore[arg-type]
            if domain not in known:
                errors.append(f"{key} row '{row_key}' names undeclared domain '{domain}'")
    for rule, domain in list(tables["prefix_rules"]):  # type: ignore[arg-type]
        if domain not in known:
            errors.append(
                f"EVE_TEST_PREFIX_DOMAIN row '{rule}' names undeclared domain '{domain}'"
            )

    # Roots must be real package directories, otherwise a rename silently drops
    # every test of that module into "unclassified".
    if MODULES_DIR.is_dir():
        present = {entry.name for entry in MODULES_DIR.iterdir() if entry.is_dir()}
        for root in dict(tables["module_domain"]):
            if root not in present:
                errors.append(
                    f"EVE_TEST_MODULE_DOMAIN root '{root}' is not a directory under "
                    f"src/modules (renamed or removed module?)"
                )

    # Prefix rules must be unambiguous by construction: if a prefix rule is also
    # the start of another rule naming a different domain, longest-match silently
    # decides. An exact ('=') rule cannot swallow a longer basename, so it is
    # exempt.
    prefix_rules = [
        (rule.lstrip("="), rule.startswith("="), domain)
        for rule, domain in list(tables["prefix_rules"])  # type: ignore[arg-type]
    ]
    for rule, exact, domain in prefix_rules:
        if exact:
            continue
        for other, other_exact, other_domain in prefix_rules:
            if rule == other or domain == other_domain:
                continue
            if not other_exact and other.startswith(rule):
                errors.append(
                    f"EVE_TEST_PREFIX_DOMAIN rule '{rule}' ({domain}) is a prefix of "
                    f"'{other}' ({other_domain}); make one of them exact with '='"
                )
    return errors


def _module_domains(
    source: str, module_domain: dict[str, str], package_override: dict[str, str]
) -> list[str]:
    """Domains named by the module headers this source includes, most-included
    first.

    Ranking by include count rather than by first appearance keeps an incidental
    early include from deciding the domain: a test that pulls one `card/` header
    for a fixture but twelve `definitions/` headers for its subject belongs with
    `definitions`, not with `card`. Ties keep first-appearance order.
    """
    counts: dict[str, int] = {}
    order: list[str] = []
    for match in MODULE_INCLUDE_RE.finditer(source):
        parts = match.group(1).split("/")
        if len(parts) < 2:
            continue
        two_segment = "/".join(parts[:2])
        if two_segment in package_override:
            domain = package_override[two_segment]
        elif parts[0] in module_domain:
            domain = module_domain[parts[0]]
        else:
            continue
        if domain not in counts:
            counts[domain] = 0
            order.append(domain)
        counts[domain] += 1
    return sorted(order, key=lambda domain: (-counts[domain], order.index(domain)))


def classify(
    basename: str, source: str, tables: dict[str, object]
) -> tuple[str | None, str, str | None]:
    """Return (domain, reason, error) for one test source."""
    if basename == SHARED_RUNNER:
        return None, "shared runner", None

    module_domain = dict(tables["module_domain"])  # type: ignore[arg-type]
    package_override = dict(tables["package_override"])  # type: ignore[arg-type]
    by_include = _module_domains(source, module_domain, package_override)
    if by_include:
        return by_include[0], f"include:{by_include[0]}", None

    hits: list[tuple[int, str, str]] = []
    for rule, domain in list(tables["prefix_rules"]):  # type: ignore[arg-type]
        if rule.startswith("="):
            if basename == rule[1:]:
                hits.append((len(rule) - 1, domain, rule))
        elif basename.startswith(rule):
            hits.append((len(rule), domain, rule))
    if not hits:
        return None, "", "no include names a module root and no basename rule matches"
    hits.sort(key=lambda hit: hit[0], reverse=True)
    longest = hits[0][0]
    winners = {domain for length, domain, _ in hits if length == longest}
    if len(winners) > 1:
        rules = ", ".join(sorted(rule for length, _, rule in hits if length == longest))
        return None, "", f"ambiguous: rules {rules} name {sorted(winners)}"
    return hits[0][1], f"prefix:{hits[0][2]}", None


def classify_paths(
    paths: list[Path], tables: dict[str, object]
) -> tuple[dict[str, list[Path]], list[str]]:
    """Map domain -> source paths for the given sources, plus errors."""
    buckets: dict[str, list[Path]] = {name: [] for name in tables["domains"]}  # type: ignore[arg-type]
    errors: list[str] = []
    for path in paths:
        try:
            source = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            errors.append(f"{path.name}: cannot read ({exc})")
            continue
        domain, _reason, error = classify(path.stem, source, tables)
        if error:
            errors.append(f"{path.name}: {error}")
        elif domain is not None:
            buckets[domain].append(path)
    for members in buckets.values():
        members.sort(key=lambda member: member.name)
    return buckets, errors


def partition(
    tables: dict[str, object], test_dir: Path = TEST_DIR
) -> tuple[dict[str, list[Path]], list[str]]:
    """Classify every test_dir/*.cpp; used by the checker (no configure needed)."""
    return classify_paths(sorted(test_dir.glob("*.cpp")), tables)


def emit_cmake(buckets: dict[str, list[Path]], out_path: Path) -> None:
    """Write one `set(EVE_TEST_DOMAIN_<domain>_SOURCES ...)` per non-empty domain.

    The real source paths are emitted verbatim rather than re-derived from the
    basename, so the generated partition cannot disagree with the list CMake
    globbed and filtered.
    """
    relative = [path for members in buckets.values() for path in members if not path.is_absolute()]
    if relative:
        raise ValueError(
            "test domain sources must be absolute paths; got: "
            + ", ".join(str(path) for path in relative[:5])
        )
    lines = [
        "# Generated by scripts/test_domains.py from test/test_domains.cmake.",
        "# Do not edit by hand: edit the table and reconfigure.",
        "",
    ]
    for domain, members in buckets.items():
        if not members:
            continue
        sources = "".join(f'  "{member.as_posix()}"\n' for member in members)
        lines.append(f"set(EVE_TEST_DOMAIN_{domain}_SOURCES\n{sources})\n")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        help="File with one test source path per line (the profile-filtered set). "
        "Defaults to every test/*.cpp.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the domain partition as a CMake include file.",
    )
    args = parser.parse_args(argv)

    if not DOMAIN_TABLE.exists():
        print(f"error: {DOMAIN_TABLE} not found", file=sys.stderr)
        return 1

    tables = parse_domain_table(DOMAIN_TABLE.read_text(encoding="utf-8"))
    errors = table_contract_errors(tables)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    if args.input:
        # utf-8-sig: tolerate a BOM if the list was written by a tool that adds
        # one (CMake's file(WRITE) does not, but a hand-edited list might).
        paths = [
            Path(line.strip())
            for line in args.input.read_text(encoding="utf-8-sig").splitlines()
            if line.strip()
        ]
    else:
        paths = sorted(TEST_DIR.glob("*.cpp"))

    buckets, errors = classify_paths(paths, tables)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(
            "error: every test/*.cpp must match exactly one domain in "
            "test/test_domains.cmake",
            file=sys.stderr,
        )
        return 1

    if args.output:
        try:
            emit_cmake(buckets, args.output)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        return 0

    total = sum(len(members) for members in buckets.values())
    print(f"test domains: {len(buckets)} domains, {total} tests")
    for domain, members in buckets.items():
        print(f"  {domain:14s} {len(members):4d}")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
