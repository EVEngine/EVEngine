#!/usr/bin/env bash
# Archive an existing Linux eve executable and its non-system shared libraries.
# Usage: package_smoke_runtime.sh /path/to/eve /path/to/runtime.tar.gz
set -euo pipefail
binary="$(realpath "$1")"
archive="$(realpath -m "$2")"
runtime="$(mktemp -d)"
trap 'rm -rf "$runtime"' EXIT
mkdir -p "$runtime/lib" "$(dirname "$archive")"
cp "$binary" "$runtime/eve"
# ldd includes transitive dependencies. System libraries are supplied by the
# matching Ubuntu packages on the producer and consumer runners.
ldd "$binary" | tee "$runtime/ldd.txt"
! grep -q 'not found' "$runtime/ldd.txt"
while read -r library; do
  case "$library" in
    /lib/*|/lib64/*|/usr/lib/*) ;;
    *) cp -L "$library" "$runtime/lib/" ;;
  esac
done < <(awk '/=> \// {print $3}' "$runtime/ldd.txt")
tar -czf "$archive" -C "$runtime" eve lib
