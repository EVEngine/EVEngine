#include "map/PathTopology.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace eve::map {
namespace {

constexpr float kSqrt2 = 1.41421356237f;

}  // namespace

PathTopology parsePathTopology(const std::string &name, PathTopology fallback) {
    if (name == "ortho4" || name == "orthogonal4" || name == "4") return PathTopology::Ortho4;
    if (name == "ortho8" || name == "orthogonal8" || name == "8") return PathTopology::Ortho8;
    if (name == "hex" || name == "hexagonal" || name == "staggered") return PathTopology::Hex;
    if (name == "auto") return fallback;
    return fallback;
}

std::string pathTopologyName(PathTopology t) {
    switch (t) {
    case PathTopology::Ortho4:
        return "ortho4";
    case PathTopology::Ortho8:
        return "ortho8";
    case PathTopology::Hex:
        return "hex";
    }
    return "ortho4";
}

PathTopology topologyFromOrientation(MapOrientation orientation, bool preferDiagonal) {
    switch (orientation) {
    case MapOrientation::Staggered:
    case MapOrientation::Hexagonal:
        return PathTopology::Hex;
    case MapOrientation::Orthogonal:
    case MapOrientation::Isometric:
    default:
        return preferDiagonal ? PathTopology::Ortho8 : PathTopology::Ortho4;
    }
}

void forEachNeighbor(PathTopology topology, int x, int y, bool staggerAxisY, bool staggerOdd,
                     NeighborFn fn) {
    if (!fn) return;
    switch (topology) {
    case PathTopology::Ortho4: {
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) fn(x + dx[i], y + dy[i], 1.f);
        break;
    }
    case PathTopology::Ortho8: {
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int i = 0; i < 8; ++i) {
            const float c = (i < 4) ? 1.f : kSqrt2;
            fn(x + dx[i], y + dy[i], c);
        }
        break;
    }
    case PathTopology::Hex: {
        // Tiled staggered hex neighbors (odd-r / even-r or odd-q / even-q).
        if (staggerAxisY) {
            const bool rowOdd = ((y & 1) != 0);
            const bool shifted = staggerOdd ? rowOdd : !rowOdd;
            if (shifted) {
                // Odd rows (when staggerIndex=Odd): extra +x on vertical neighbors.
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x, y - 1, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x, y + 1, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x, y + 1, 1.f);
            }
        } else {
            const bool colOdd = ((x & 1) != 0);
            const bool shifted = staggerOdd ? colOdd : !colOdd;
            if (shifted) {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x + 1, y, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x + 1, y, 1.f);
            }
        }
        break;
    }
    }
}

float pathHeuristic(PathTopology topology, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    switch (topology) {
    case PathTopology::Ortho4:
        return float(dx + dy);
    case PathTopology::Ortho8:
        return float(std::max(dx, dy)) + (kSqrt2 - 1.f) * float(std::min(dx, dy));
    case PathTopology::Hex: {
        // Convert offset → axial roughly for distance; stagger Y odd-r style.
        // Using max of cube coords after offset conversion (odd-r):
        // q = x - (y - (y&1)) / 2; r = y
        auto toCube = [](int x, int y, bool /*unused*/) {
            const int q = x - (y - (y & 1)) / 2;
            const int r = y;
            const int s = -q - r;
            return std::tuple<int, int, int>{q, r, s};
        };
        auto [q0, r0, s0] = toCube(x0, y0, true);
        auto [q1, r1, s1] = toCube(x1, y1, true);
        return float(std::max({std::abs(q0 - q1), std::abs(r0 - r1), std::abs(s0 - s1)}));
    }
    }
    return float(dx + dy);
}

}  // namespace eve::map
