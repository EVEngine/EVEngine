#!/usr/bin/env bash
# Source-only gate matching CI job `source-quality` in .github/workflows/ci.yml.
#
# Usage:
#   bash scripts/check_source_quality.sh [base-sha]
#   make check
#   make check CI_BASE=<pr-base>
#
# Does not configure or build the engine. Requires python3, ruff, pillow,
# clang-format (18) and git-clang-format. On Windows this script is meant to
# run from the root Makefile (Git Bash).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 not found on PATH" >&2
    exit 1
fi

BASE="${1:-${CI_BASE:-origin/dev}}"

group() {
    local title="$1"
    shift
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::group::${title}"
    else
        echo "==> ${title}"
    fi
    "$@"
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::endgroup::"
    fi
}

if ! python3 -m ruff --version >/dev/null 2>&1; then
    echo "error: ruff is required for the same gate CI runs (python3 -m ruff check scripts)" >&2
    echo "  python3 -m pip install ruff pillow" >&2
    exit 1
fi

group "changed lines are clang-format clean" \
    bash .github/scripts/check-format.sh "$BASE"
group "module dependency layering" \
    python3 scripts/module_depgraph.py --check
group "script bindings are documented" \
    python3 scripts/check_bindings.py --strict
group "binding gap ownership metadata" \
    python3 scripts/check_binding_gap_metadata.py
group "declared module layers vs includes" \
    python3 scripts/module_depgraph.py --check-layers
group "test source manifest" \
    python3 scripts/check_test_manifest.py
group "examples layout and overview registration" \
    python3 scripts/check_examples.py
group "critical return-value diagnostics" \
    python3 scripts/check_nodiscard.py
group "release version consistency" \
    python3 scripts/release.py check-versions
group "bounded quality debt" \
    python3 scripts/check_quality_metadata.py
group "profile matrix (source-only)" \
    python3 scripts/profile_matrix.py --check
group "architecture contracts" \
    python3 scripts/check_architecture_contracts.py --base "$BASE"
group "architecture contract fixtures" \
    python3 -X utf8 -m unittest scripts.tests.test_architecture_contracts -v
group "ruff lint repository scripts" \
    python3 -m ruff check scripts
group "repository script tests" \
    python3 -X utf8 -m unittest discover -s scripts/tests -p "test_*.py" -v

echo "ok: source-quality gate passed (base=${BASE})"
