#!/usr/bin/env bash
# Local smoke: DTL selective includes + DrawJagged compat (Apple Clang failure mode).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPAT="$ROOT/src/modules/procgen/dtl_compat"
DTL="$ROOT/external/DungeonTemplateLibrary/include"
WORKDIR="${TMPDIR:-/tmp}/eve-procgen-dtl-smoke"
mkdir -p "$WORKDIR"

cat >"$WORKDIR/smoke.cpp" <<'EOF'
#include <DTL/Base/RogueLike.hpp>
#include <DTL/Random/RandomEngine.hpp>
#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include <DTL/Shape/MazeDig.hpp>
#include <DTL/Shape/PerlinIsland.hpp>
#include <DTL/Shape/SimpleRogueLike.hpp>
#include <DTL/Utility/DrawJagged.hpp>
#include <cstdint>
#include <vector>

struct Dummy : dtl::utility::DrawJagged<Dummy, int> {
    template <typename... A>
    bool drawNormal(A &&...) const {
        return true;
    }
};

int main() {
    std::vector<std::vector<std::uint_fast8_t>> m(21, std::vector<std::uint_fast8_t>(21, 0));
    dtl::shape::SimpleRogueLike<std::uint_fast8_t> dungeon(1, 2, 3, 4, 5, 2, 5, 2);
    if (!dungeon.drawSEED(m, 42u)) return 1;
    dtl::shape::CellularAutomatonIsland<std::uint_fast8_t> cave(1, 0, 5, 0.45);
    DTL_RANDOM_ENGINE.seed(7);
    DTL_RANDOM_ENGINE.clear();
    if (!cave.draw(m)) return 2;
    dtl::shape::MazeDig<std::uint_fast8_t> maze(1, 0);
    if (!maze.drawSEED(m, 7u)) return 3;
    dtl::shape::PerlinIsland<std::uint_fast8_t> terrain(6.0, 4, 9, 0);
    if (!terrain.drawSEED(m, 9u)) return 4;

    // Force the upstream-broken createOperatorArray path (compat must provide drawOperatorArray).
    std::vector<std::vector<int>> m2(2, std::vector<int>(2, 0));
    Dummy dummy;
    dummy.createOperatorArray(m2, 1, 1, [](int) { return true; });
    return 0;
}
EOF

run_one() {
    local cxx="$1"
    local out="$WORKDIR/smoke_$2"
    local -a flags=(-std=c++20 -I"$COMPAT" -I"$DTL")
    if [[ "$cxx" == clang++* ]]; then
        flags+=(-I/usr/include/c++/13 -I/usr/include/x86_64-linux-gnu/c++/13)
    fi
    echo "==> $cxx"
    if [[ "$cxx" == clang++* ]]; then
        # Compile with clang (Apple-like strictness on this path), link with g++ for libstdc++.
        "$cxx" "${flags[@]}" -c "$WORKDIR/smoke.cpp" -o "$WORKDIR/smoke_$2.o"
        g++-13 "$WORKDIR/smoke_$2.o" -o "$out"
    else
        "$cxx" "${flags[@]}" "$WORKDIR/smoke.cpp" -o "$out"
    fi
    "$out"
    echo "OK $cxx"
}

run_one g++-13 gcc
run_one clang++ clang
echo "procgen DTL smoke passed"
