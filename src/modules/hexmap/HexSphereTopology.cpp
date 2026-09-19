#include "hexmap/HexSphereTopology.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::hexmap {
namespace {

using Vec = HexVec3;

/** @brief Golden ratio, the edge/radius proportion of an icosahedron. */
constexpr float kGoldenRatio = 1.618033988749895f;

/** @brief Dot product of two direction vectors. */
[[nodiscard]] constexpr float dot(Vec a, Vec b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** @brief Cross product of two direction vectors. */
[[nodiscard]] constexpr Vec cross(Vec a, Vec b) noexcept {
    return Vec{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/** @brief Euclidean length of a vector. */
[[nodiscard]] float length(Vec a) noexcept { return std::sqrt(dot(a, a)); }

/**
 * @brief Unit vector in the direction of `a`.
 *
 * A degenerate input cannot occur for the construction below (every midpoint of
 * two distinct unit vertices is non-zero), so the fallback only keeps the
 * function total; it is not a recoverable geometry case.
 */
[[nodiscard]] Vec normalize(Vec a) noexcept {
    const float len = length(a);
    if (len <= 1e-20f) return Vec{0.f, 1.f, 0.f};
    return Vec{a.x / len, a.y / len, a.z / len};
}

/** @brief The twelve icosahedron vertices: `(0, +-1, +-phi)` and its cyclic permutations. */
constexpr float kIcosahedronVertices[12][3] = {
    {-1.f, kGoldenRatio, 0.f},  {1.f, kGoldenRatio, 0.f},   {-1.f, -kGoldenRatio, 0.f}, {1.f, -kGoldenRatio, 0.f},
    {0.f, -1.f, kGoldenRatio},  {0.f, 1.f, kGoldenRatio},   {0.f, -1.f, -kGoldenRatio}, {0.f, 1.f, -kGoldenRatio},
    {kGoldenRatio, 0.f, -1.f},  {kGoldenRatio, 0.f, 1.f},   {-kGoldenRatio, 0.f, -1.f}, {-kGoldenRatio, 0.f, 1.f},
};

/**
 * @brief The twenty icosahedron faces as vertex triples.
 *
 * The table carries no winding guarantee; `build` flips each triangle whose
 * normal points inward, so the table can be transcribed from any source without
 * having to be re-derived by hand.
 */
constexpr std::int32_t kIcosahedronFaces[20][3] = {
    {0, 11, 5}, {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9},  {5, 11, 4}, {11, 10, 2},
    {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},  {4, 9, 5},
    {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1},
};

/** @brief One oriented primal triangle (a geodesic-sphere face). */
struct Triangle {
    std::int32_t a = 0;
    std::int32_t b = 0;
    std::int32_t c = 0;
};

/** @brief Order-independent key of the undirected edge `(i, j)`. */
[[nodiscard]] std::uint64_t edgeKey(std::int32_t i, std::int32_t j) noexcept {
    const std::uint32_t low  = static_cast<std::uint32_t>(i < j ? i : j);
    const std::uint32_t high = static_cast<std::uint32_t>(i < j ? j : i);
    return (static_cast<std::uint64_t>(high) << 32u) | low;
}

/** @brief The two primal faces carrying one edge. */
struct EdgeFaces {
    std::int32_t first  = -1;
    std::int32_t second = -1;
};

/** @brief A construction failure; every case is an internal-consistency error. */
[[nodiscard]] Result<HexSphereTopology> sphereFailure(const char* message) {
    return Result<HexSphereTopology>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "hexmap.sphere"));
}

}  // namespace

Result<HexSphereTopology> HexSphereTopology::build(std::int32_t subdivision) {
    if (subdivision < 0 || subdivision > kMaxHexSphereSubdivision) {
        return sphereFailure("hex sphere subdivision out of range");
    }

    HexSphereTopology topology;
    topology.subdivision_ = subdivision;
    topology.frequency_   = std::int32_t{1} << subdivision;

    // --- primal polyhedron --------------------------------------------------
    std::vector<Vec> vertices;
    vertices.reserve(12u);
    for (const auto& vertex : kIcosahedronVertices) vertices.push_back(normalize(Vec{vertex[0], vertex[1], vertex[2]}));

    std::vector<Triangle> triangles;
    triangles.reserve(20u);
    for (const auto& face : kIcosahedronFaces) triangles.push_back(Triangle{face[0], face[1], face[2]});
    for (Triangle& triangle : triangles) {
        const Vec normal  = cross(vertices[triangle.b] - vertices[triangle.a], vertices[triangle.c] - vertices[triangle.a]);
        const Vec outward = vertices[triangle.a] + vertices[triangle.b] + vertices[triangle.c];
        if (dot(normal, outward) < 0.f) std::swap(triangle.b, triangle.c);
    }

    // --- midpoint subdivision, frequency 2^subdivision -----------------------
    //
    // Splitting every triangle into four and welding the shared midpoints by edge
    // key keeps one vertex per physical point, so vertex ids are stable and the
    // original twelve keep their ids 0..11 -- which is what makes the twelve
    // pentagons addressable as cells [0, 12).
    for (std::int32_t level = 0; level < subdivision; ++level) {
        std::unordered_map<std::uint64_t, std::int32_t> midpoints;
        midpoints.reserve(vertices.size() * 2u);
        std::vector<Triangle> refined;
        refined.reserve(triangles.size() * 4u);

        const auto midpoint = [&](std::int32_t i, std::int32_t j) {
            const std::uint64_t key   = edgeKey(i, j);
            const auto          found = midpoints.find(key);
            if (found != midpoints.end()) return found->second;
            const std::int32_t index = static_cast<std::int32_t>(vertices.size());
            vertices.push_back(normalize(vertices[i] + vertices[j]));
            midpoints.emplace(key, index);
            return index;
        };

        for (const Triangle& triangle : triangles) {
            const std::int32_t ab = midpoint(triangle.a, triangle.b);
            const std::int32_t bc = midpoint(triangle.b, triangle.c);
            const std::int32_t ca = midpoint(triangle.c, triangle.a);
            refined.push_back(Triangle{triangle.a, ab, ca});
            refined.push_back(Triangle{ab, triangle.b, bc});
            refined.push_back(Triangle{ca, bc, triangle.c});
            refined.push_back(Triangle{ab, bc, ca});
        }
        triangles.swap(refined);
    }

    const std::int32_t cellCount = static_cast<std::int32_t>(vertices.size());
    topology.directions_         = std::move(vertices);

    // --- dual corners: one per primal face ----------------------------------
    std::vector<Vec> cornerDirections(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const Triangle& triangle = triangles[i];
        cornerDirections[i] =
            normalize(topology.directions_[triangle.a] + topology.directions_[triangle.b] + topology.directions_[triangle.c]);
    }

    // --- vertex -> incident face incidence, in CSR form ----------------------
    const std::int32_t        faceCount = static_cast<std::int32_t>(triangles.size());
    std::vector<std::int32_t> incidenceCount(static_cast<std::size_t>(cellCount), 0);
    for (const Triangle& triangle : triangles) {
        ++incidenceCount[triangle.a];
        ++incidenceCount[triangle.b];
        ++incidenceCount[triangle.c];
    }
    std::vector<std::int32_t> incidenceOffset(static_cast<std::size_t>(cellCount) + 1u, 0);
    for (std::int32_t cell = 0; cell < cellCount; ++cell) {
        incidenceOffset[cell + 1] = incidenceOffset[cell] + incidenceCount[cell];
    }
    std::vector<std::int32_t> incidence(static_cast<std::size_t>(incidenceOffset[cellCount]), -1);
    {
        std::vector<std::int32_t> cursor(incidenceOffset.begin(), incidenceOffset.end() - 1);
        for (std::int32_t face = 0; face < faceCount; ++face) {
            const Triangle& triangle = triangles[face];
            incidence[cursor[triangle.a]++] = face;
            incidence[cursor[triangle.b]++] = face;
            incidence[cursor[triangle.c]++] = face;
        }
    }

    // --- edge -> its one or two faces ---------------------------------------
    std::unordered_map<std::uint64_t, EdgeFaces> edgeFaces;
    edgeFaces.reserve(triangles.size() * 3u);
    for (std::int32_t face = 0; face < faceCount; ++face) {
        const Triangle& triangle = triangles[face];
        const std::int32_t pairs[3][2] = {{triangle.a, triangle.b}, {triangle.b, triangle.c}, {triangle.c, triangle.a}};
        for (const auto& pair : pairs) {
            EdgeFaces& entry = edgeFaces[edgeKey(pair[0], pair[1])];
            if (entry.first < 0) entry.first = face;
            else if (entry.second < 0) entry.second = face;
        }
    }

    const auto slotOf = [&](std::int32_t face, std::int32_t vertex) {
        const Triangle& triangle = triangles[face];
        if (triangle.a == vertex) return 0;
        if (triangle.b == vertex) return 1;
        return 2;
    };
    const auto vertexAt = [&](std::int32_t face, std::int32_t slot) {
        const Triangle& triangle = triangles[face];
        if (slot == 0) return triangle.a;
        if (slot == 1) return triangle.b;
        return triangle.c;
    };
    const auto nextVertexInFace = [&](std::int32_t face, std::int32_t vertex) {
        return vertexAt(face, (slotOf(face, vertex) + 1) % 3);
    };
    const auto otherFaceOfEdge = [&](std::int32_t face, std::int32_t i, std::int32_t j) {
        const auto found = edgeFaces.find(edgeKey(i, j));
        if (found == edgeFaces.end()) return -1;
        if (found->second.first == face) return found->second.second;
        if (found->second.second == face) return found->second.first;
        return -1;
    };

    // --- rotate around every vertex to get its cyclic corner and edge order ---
    topology.cellOffsets_.assign(static_cast<std::size_t>(cellCount) + 1u, 0);
    topology.neighbors_.assign(static_cast<std::size_t>(incidenceOffset[cellCount]), 0);
    topology.corners_.assign(topology.neighbors_.size(), 0);

    std::vector<std::int32_t> orderedFaces(6, -1);
    for (std::int32_t cell = 0; cell < cellCount; ++cell) {
        const std::int32_t begin  = incidenceOffset[cell];
        const std::int32_t degree = incidenceOffset[cell + 1] - begin;
        if (degree < 5 || degree > 6) return sphereFailure("hex sphere cell has an unsupported edge count");
        topology.cellOffsets_[cell] = begin;

        const std::int32_t firstFace = incidence[begin];
        std::int32_t       current   = firstFace;
        for (std::int32_t direction = 0; direction < degree; ++direction) {
            orderedFaces[direction] = current;
            const std::int32_t nextFace = otherFaceOfEdge(current, cell, nextVertexInFace(current, cell));
            if (nextFace < 0) return sphereFailure("hex sphere half-edge walk left the surface");
            current = nextFace;
        }
        if (current != firstFace) return sphereFailure("hex sphere half-edge walk did not close");

        for (std::int32_t direction = 0; direction < degree; ++direction) {
            topology.corners_[begin + direction]   = orderedFaces[direction];
            topology.neighbors_[begin + direction] = nextVertexInFace(orderedFaces[direction], cell);
        }
    }
    topology.cellOffsets_[cellCount] = static_cast<std::int32_t>(topology.neighbors_.size());
    topology.cornerDirections_       = std::move(cornerDirections);

    // --- handedness ---------------------------------------------------------
    //
    // Walking "the other face of the edge to the next vertex" is a uniform rule,
    // so every cell comes out with the same handedness and the only question is
    // which one. The shared edge of two adjacent cells must expose the same two
    // corners from both sides, in reversed order; if it does not, reversing every
    // cell's cyclic order flips the handedness of all of them at once.
    const auto cornersAgree = [&topology]() {
        for (std::int32_t cell = 0; cell < topology.cellCount(); ++cell) {
            const std::int32_t degree = topology.neighborCount(cell);
            for (std::int32_t direction = 0; direction < degree; ++direction) {
                const HexSphereCell other = topology.neighbor(cell, direction);
                const std::int32_t  back  = topology.directionOf(other, cell);
                if (back < 0) return false;
                const std::int32_t otherDegree = topology.neighborCount(other);
                const std::int32_t here       = topology.corners_[topology.cellOffsets_[cell] + direction];
                const std::int32_t hereNext  = topology.corners_[topology.cellOffsets_[cell] + (direction + 1) % degree];
                const std::int32_t there      = topology.corners_[topology.cellOffsets_[other] + back];
                const std::int32_t thereNext = topology.corners_[topology.cellOffsets_[other] + (back + 1) % otherDegree];
                if (here != thereNext || hereNext != there) return false;
            }
        }
        return true;
    };

    if (!cornersAgree()) {
        for (std::int32_t cell = 0; cell < cellCount; ++cell) {
            const auto begin  = topology.neighbors_.begin() + topology.cellOffsets_[cell];
            const auto end    = topology.neighbors_.begin() + topology.cellOffsets_[cell + 1];
            const auto cornerBegin = topology.corners_.begin() + topology.cellOffsets_[cell];
            const auto cornerEnd   = topology.corners_.begin() + topology.cellOffsets_[cell + 1];
            std::reverse(begin, end);
            std::reverse(cornerBegin, cornerEnd);
        }
        if (!cornersAgree()) return sphereFailure("hex sphere winding is inconsistent");
    }

    // --- Euler characteristic ------------------------------------------------
    if (topology.cellCount() - topology.edgeCount() + topology.cornerCount() != 2) {
        return sphereFailure("hex sphere does not close into a sphere");
    }

    return Result<HexSphereTopology>::success(std::move(topology));
}

std::int32_t HexSphereTopology::neighborCount(HexSphereCell cell) const noexcept {
    if (!contains(cell)) return 0;
    return cellOffsets_[static_cast<std::size_t>(cell) + 1u] - cellOffsets_[static_cast<std::size_t>(cell)];
}

HexSphereCell HexSphereTopology::neighbor(HexSphereCell cell, std::int32_t direction) const noexcept {
    if (direction < 0 || direction >= neighborCount(cell)) return kNoHexSphereCell;
    return neighbors_[static_cast<std::size_t>(cellOffsets_[static_cast<std::size_t>(cell)] + direction)];
}

std::int32_t HexSphereTopology::directionOf(HexSphereCell cell, HexSphereCell other) const noexcept {
    if (!contains(cell) || !contains(other)) return kNoHexSphereCell;
    const std::int32_t begin  = cellOffsets_[static_cast<std::size_t>(cell)];
    const std::int32_t degree = neighborCount(cell);
    for (std::int32_t direction = 0; direction < degree; ++direction) {
        if (neighbors_[static_cast<std::size_t>(begin + direction)] == other) return direction;
    }
    return kNoHexSphereCell;
}

std::int32_t HexSphereTopology::oppositeDirection(HexSphereCell cell, std::int32_t direction) const noexcept {
    const HexSphereCell other = neighbor(cell, direction);
    if (other == kNoHexSphereCell) return kNoHexSphereCell;
    return directionOf(other, cell);
}

HexVec3 HexSphereTopology::direction(HexSphereCell cell) const noexcept {
    if (!contains(cell)) return HexVec3{0.f, 1.f, 0.f};
    return directions_[static_cast<std::size_t>(cell)];
}

std::int32_t HexSphereTopology::cornerCountOf(HexSphereCell cell) const noexcept { return neighborCount(cell); }

HexVec3 HexSphereTopology::corner(HexSphereCell cell, std::int32_t cornerIndex) const noexcept {
    if (cornerIndex < 0 || cornerIndex >= neighborCount(cell)) return HexVec3{0.f, 1.f, 0.f};
    const std::int32_t id = corners_[static_cast<std::size_t>(cellOffsets_[static_cast<std::size_t>(cell)] + cornerIndex)];
    if (id < 0 || id >= cornerCount()) return HexVec3{0.f, 1.f, 0.f};
    return cornerDirections_[static_cast<std::size_t>(id)];
}

HexSphereCell HexSphereTopology::cellAt(HexVec3 unitDirection) const noexcept {
    if (empty()) return kNoHexSphereCell;
    const Vec target = normalize(unitDirection);

    HexSphereCell current = 0;
    float         best    = dot(directions_[0], target);
    const std::int32_t seeds = cellCount() < 12 ? cellCount() : 12;
    for (std::int32_t cell = 1; cell < seeds; ++cell) {
        const float score = dot(directions_[static_cast<std::size_t>(cell)], target);
        if (score > best) {
            best    = score;
            current = cell;
        }
    }

    for (;;) {
        bool improved = false;
        const std::int32_t degree = neighborCount(current);
        for (std::int32_t direction = 0; direction < degree; ++direction) {
            const HexSphereCell candidate =
                neighbors_[static_cast<std::size_t>(cellOffsets_[static_cast<std::size_t>(current)] + direction)];
            const float score = dot(directions_[static_cast<std::size_t>(candidate)], target);
            if (score > best + 1e-7f) {
                best      = score;
                current   = candidate;
                improved  = true;
            }
        }
        if (!improved) break;
    }
    return current;
}

float HexSphereTopology::angularDistance(HexVec3 a, HexVec3 b) noexcept {
    const float cosine = dot(normalize(a), normalize(b));
    return std::acos(std::clamp(cosine, -1.f, 1.f));
}

}  // namespace eve::hexmap
