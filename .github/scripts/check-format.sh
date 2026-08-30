#!/usr/bin/env bash
# Fail when the PR's changed lines are not clang-format clean.
#
# Usage: bash .github/scripts/check-format.sh [base-sha]
#   base-sha defaults to origin/dev; for pull_request events CI passes
#   github.event.pull_request.base.sha so the check is stable even while dev
#   moves.
#
# Only changed lines of *existing* files are checked (git clang-format), so
# pre-existing formatting debt in untouched regions does not block a PR.
# Brand-new files are skipped with a warning: the legacy codebase is not yet
# clang-format clean, and files created by mechanical refactors (splits/moves)
# inherit that style. Format new files by hand; tighten this once the repo has
# been formatted once.
set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found on PATH" >&2
    echo "  CI installs clang-format-18 and symlinks it; develop locally with:" >&2
    echo "    sudo apt-get install -y clang-format-18 && sudo ln -sf /usr/bin/clang-format-18 /usr/local/bin/clang-format" >&2
    exit 1
fi

BASE="${1:-origin/dev}"
if ! git rev-parse --verify --quiet "$BASE" >/dev/null; then
    echo "warning: base '$BASE' is not a commit; falling back to origin/dev" >&2
    BASE="origin/dev"
fi
if ! git rev-parse --verify --quiet "$BASE" >/dev/null; then
    echo "warning: origin/dev not available; skipping format check" >&2
    exit 0
fi

MERGE_BASE="$(git merge-base "$BASE" HEAD)"
DIFF_FILE="$(mktemp)"
trap 'rm -f "$DIFF_FILE"' EXIT

CHANGED_FILES="$(git diff --name-only "$MERGE_BASE" HEAD -- '*.cpp' '*.h' '*.hpp')"
if [ -z "$CHANGED_FILES" ]; then
    echo "ok: no C/C++ files changed"
    exit 0
fi

for file in $CHANGED_FILES; do
    if ! git cat-file -e "$MERGE_BASE:$file" 2>/dev/null; then
        echo "warning: new file '$file' is not checked (format it by hand)"
        continue
    fi
    # --diff exits non-zero when differences are found; keep going so we can
    # print the diff as the failure message.
    git clang-format --quiet --diff "$MERGE_BASE" -- "$file" >>"$DIFF_FILE" || true
done

if [ -s "$DIFF_FILE" ]; then
    echo "error: changed lines are not clang-format clean (see .clang-format):" >&2
    cat "$DIFF_FILE" >&2
    exit 1
fi

echo "ok: changed lines are clang-format clean"
