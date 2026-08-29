#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <install-prefix> <platform> <build-type>" >&2
  exit 2
fi

prefix="$1"
expected_platform="$2"
expected_build_type="$3"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pin_file="$repo_root/cmake/third_party_pin.cmake"
version_file="$prefix/third-party-version.txt"

for required_dir in include lib; do
  if [ ! -d "$prefix/$required_dir" ]; then
    echo "third-party cache is missing $prefix/$required_dir" >&2
    exit 1
  fi
done
if [ ! -f "$version_file" ]; then
  echo "third-party cache is missing $version_file" >&2
  exit 1
fi

expected_aggregate="$(sed -nE \
  's/^[[:space:]]*set\(EVENGINE_THIRD_PARTY_PIN "([0-9a-f]{40})".*/\1/p' \
  "$pin_file")"
if [ -z "$expected_aggregate" ]; then
  echo "could not read EVENGINE_THIRD_PARTY_PIN from $pin_file" >&2
  exit 1
fi

read_field() {
  local field="$1"
  sed -n "s/^${field}=//p" "$version_file" | head -n 1
}

actual_platform="$(read_field platform)"
actual_build_type="$(read_field build_type)"
actual_aggregate="$(read_field aggregate)"
actual_dirty="$(read_field dirty)"

if [ "$actual_platform" != "$expected_platform" ]; then
  echo "third-party platform mismatch: expected $expected_platform, got ${actual_platform:-<missing>}" >&2
  exit 1
fi
if [ "$actual_build_type" != "$expected_build_type" ]; then
  echo "third-party build type mismatch: expected $expected_build_type, got ${actual_build_type:-<missing>}" >&2
  exit 1
fi
if [ "$actual_aggregate" != "$expected_aggregate" ]; then
  echo "third-party aggregate mismatch: expected $expected_aggregate, got ${actual_aggregate:-<missing>}" >&2
  exit 1
fi
if [[ ! "$actual_dirty" =~ ^[01]$ ]]; then
  echo "third-party version stamp has invalid dirty value: ${actual_dirty:-<missing>}" >&2
  exit 1
fi

echo "third-party cache verified: $expected_platform/$expected_build_type@$actual_aggregate (dirty=$actual_dirty)"
