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

if [[ "${SOURCE_QUALITY_MODE:-full}" =~ ^(full|scripts)$ ]] && ! python3 -m ruff --version >/dev/null 2>&1; then
    echo "error: ruff is required for the same gate CI runs (python3 -m ruff check scripts)" >&2
    echo "  python3 -m pip install ruff pillow" >&2
    exit 1
fi

if [ "${SOURCE_QUALITY_MODE:-full}" = "full" ]; then
    group "changed lines are clang-format clean" \
        bash .github/scripts/check-format.sh "$BASE"
fi
# Bound subprocess parallelism; logs are emitted as complete groups.
# Keep formatting first, then share one worker pool across checks and tests.
python3 scripts/run_source_quality.py --base "$BASE" --jobs "${SOURCE_QUALITY_JOBS:-4}"

echo "ok: source-quality gate passed (base=${BASE})"
