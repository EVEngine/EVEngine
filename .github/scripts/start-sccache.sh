#!/usr/bin/env bash
# Start sccache with retries; on persistent GHA/ghac storage failures switch
# to a local disk cache so a transient Actions-cache egress throttle does not
# fail the whole job. Writes SCCACHE_GHA_ENABLED=false to GITHUB_ENV when the
# local-only path is taken so later steps keep that configuration.
set -euo pipefail

MAX_ATTEMPTS="${SCCACHE_START_ATTEMPTS:-3}"
SLEEP_SECS="${SCCACHE_START_RETRY_SLEEP:-5}"

is_already_running() {
  local log="$1"
  grep -qiE 'already running|server is already running' "$log"
}

is_transient_storage_error() {
  local log="$1"
  grep -qiE 'ServerBusy|Egress is over the account limit|Unexpected \(temporary\)|status: 503|service: ghac' "$log"
}

try_start() {
  local log
  log="$(mktemp)"
  set +e
  sccache --start-server >"$log" 2>&1
  local rc=$?
  set -e
  cat "$log"
  if [ "$rc" -eq 0 ] || is_already_running "$log"; then
    rm -f "$log"
    return 0
  fi
  if is_transient_storage_error "$log"; then
    rm -f "$log"
    return 2
  fi
  rm -f "$log"
  return 1
}

attempt=1
while [ "$attempt" -le "$MAX_ATTEMPTS" ]; do
  rc=0
  try_start || rc=$?
  if [ "$rc" -eq 0 ]; then
    exit 0
  fi
  if [ "$rc" -eq 1 ]; then
    echo "sccache --start-server failed with a non-transient error" >&2
    exit 1
  fi
  if [ "$attempt" -ge "$MAX_ATTEMPTS" ]; then
    break
  fi
  echo "sccache GHA storage unavailable (attempt ${attempt}/${MAX_ATTEMPTS}); retrying in ${SLEEP_SECS}s..."
  sleep "$SLEEP_SECS"
  attempt=$((attempt + 1))
done

echo "::warning::sccache GHA/ghac storage still unavailable after ${MAX_ATTEMPTS} attempts; using local disk cache"
sccache --stop-server >/dev/null 2>&1 || true
export SCCACHE_GHA_ENABLED=false
unset SCCACHE_GHA_VERSION SCCACHE_GHA_RW_MODE || true
if [ -n "${GITHUB_ENV:-}" ]; then
  {
    echo "SCCACHE_GHA_ENABLED=false"
    echo "SCCACHE_GHA_VERSION="
    echo "SCCACHE_GHA_RW_MODE="
  } >>"$GITHUB_ENV"
fi

if ! sccache --start-server; then
  echo "sccache --start-server failed even with local disk cache" >&2
  exit 1
fi
