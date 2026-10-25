// eve-hot-path
#include "hexmap/HexSphereTopology.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "hexmap/HexMapMesh.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"
#include "hexmap/HexTerrainBake.h"

// HexSphereTopology.cpp
namespace eve::hexmap {
namespace {

using Vec = HexVec3;

/** @brief Golden ratio, the edge/radius proportion of an icosahedron. */
constexpr float kGoldenRatio = 1.618033988749895f;

/** @brief Dot product of two direction vectors. */
[[nodiscard]] constexpr float dot(Vec a, Vec b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

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
    {-1.f, kGoldenRatio, 0.f}, {1.f, kGoldenRatio, 0.f}, {-1.f, -kGoldenRatio, 0.f}, {1.f, -kGoldenRatio, 0.f},
    {0.f, -1.f, kGoldenRatio}, {0.f, 1.f, kGoldenRatio}, {0.f, -1.f, -kGoldenRatio}, {0.f, 1.f, -kGoldenRatio},
    {kGoldenRatio, 0.f, -1.f}, {kGoldenRatio, 0.f, 1.f}, {-kGoldenRatio, 0.f, -1.f}, {-kGoldenRatio, 0.f, 1.f},
};

/**
 * @brief The twenty icosahedron faces as vertex triples.
 *
 * The table carries no winding guarantee; `build` flips each triangle whose
 * normal points inward, so the table can be transcribed from any source without
 * having to be re-derived by hand.
 */
constexpr std::int32_t kIcosahedronFaces[20][3] = {
    {0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
    {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
    {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1},
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
        const Vec normal =
            cross(vertices[triangle.b] - vertices[triangle.a], vertices[triangle.c] - vertices[triangle.a]);
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
        cornerDirections[i]      = normalize(topology.directions_[triangle.a] + topology.directions_[triangle.b] +
                                             topology.directions_[triangle.c]);
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
            const Triangle& triangle        = triangles[face];
            incidence[cursor[triangle.a]++] = face;
            incidence[cursor[triangle.b]++] = face;
            incidence[cursor[triangle.c]++] = face;
        }
    }

    // --- edge -> its one or two faces ---------------------------------------
    std::unordered_map<std::uint64_t, EdgeFaces> edgeFaces;
    edgeFaces.reserve(triangles.size() * 3u);
    for (std::int32_t face = 0; face < faceCount; ++face) {
        const Triangle&    triangle    = triangles[face];
        const std::int32_t pairs[3][2] = {{triangle.a, triangle.b}, {triangle.b, triangle.c}, {triangle.c, triangle.a}};
        for (const auto& pair : pairs) {
            EdgeFaces& entry = edgeFaces[edgeKey(pair[0], pair[1])];
            if (entry.first < 0)
                entry.first = face;
            else if (entry.second < 0)
                entry.second = face;
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
            orderedFaces[direction]     = current;
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
                const std::int32_t here        = topology.corners_[topology.cellOffsets_[cell] + direction];
                const std::int32_t hereNext = topology.corners_[topology.cellOffsets_[cell] + (direction + 1) % degree];
                const std::int32_t there    = topology.corners_[topology.cellOffsets_[other] + back];
                const std::int32_t thereNext =
                    topology.corners_[topology.cellOffsets_[other] + (back + 1) % otherDegree];
                if (here != thereNext || hereNext != there) return false;
            }
        }
        return true;
    };

    if (!cornersAgree()) {
        for (std::int32_t cell = 0; cell < cellCount; ++cell) {
            const auto begin       = topology.neighbors_.begin() + topology.cellOffsets_[cell];
            const auto end         = topology.neighbors_.begin() + topology.cellOffsets_[cell + 1];
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
    const std::int32_t id =
        corners_[static_cast<std::size_t>(cellOffsets_[static_cast<std::size_t>(cell)] + cornerIndex)];
    if (id < 0 || id >= cornerCount()) return HexVec3{0.f, 1.f, 0.f};
    return cornerDirections_[static_cast<std::size_t>(id)];
}

HexSphereCell HexSphereTopology::cellAt(HexVec3 unitDirection) const noexcept {
    if (empty()) return kNoHexSphereCell;
    const Vec target = normalize(unitDirection);

    HexSphereCell      current = 0;
    float              best    = dot(directions_[0], target);
    const std::int32_t seeds   = cellCount() < 12 ? cellCount() : 12;
    for (std::int32_t cell = 1; cell < seeds; ++cell) {
        const float score = dot(directions_[static_cast<std::size_t>(cell)], target);
        if (score > best) {
            best    = score;
            current = cell;
        }
    }

    for (;;) {
        bool               improved = false;
        const std::int32_t degree   = neighborCount(current);
        for (std::int32_t direction = 0; direction < degree; ++direction) {
            const HexSphereCell candidate =
                neighbors_[static_cast<std::size_t>(cellOffsets_[static_cast<std::size_t>(current)] + direction)];
            const float score = dot(directions_[static_cast<std::size_t>(candidate)], target);
            if (score > best + 1e-7f) {
                best     = score;
                current  = candidate;
                improved = true;
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

// HexTerrainBake.cpp
/**
 * @file HexTerrainBake.cpp
 * @brief Encode, decode and apply a hex terrain bake without generating one.
 */


namespace eve::hexmap {
namespace {

[[nodiscard]] [[nodiscard]] Result<HexTerrainBake> invalidBake(std::string message) {
    return Result<HexTerrainBake>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "hexmap"));
}

void writeU32LE(unsigned char* bytes, std::uint32_t value) noexcept {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

[[nodiscard]] std::uint32_t readU32LE(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] std::string packCells(const std::vector<HexCellData>& cells) {
    std::string packed(cells.size() * 8u, '\0');
    auto*       bytes = reinterpret_cast<unsigned char*>(packed.data());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        writeU32LE(bytes + i * 8u, cells[i].values.raw());
        writeU32LE(bytes + i * 8u + 4u, cells[i].flags.raw());
    }
    return packed;
}

[[nodiscard]] Result<std::vector<HexCellData>> unpackCells(std::string_view packed, std::size_t expected) {
    if (packed.size() != expected * 8u)
        return Result<std::vector<HexCellData>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "hex terrain bake cell payload length does not match the cell count", "hexmap"));
    std::vector<HexCellData> cells(expected);
    const auto*              bytes = reinterpret_cast<const unsigned char*>(packed.data());
    for (std::size_t i = 0; i < expected; ++i) {
        cells[i].values = HexValues{readU32LE(bytes + i * 8u)};
        cells[i].flags  = HexFlags{readU32LE(bytes + i * 8u + 4u)};
    }
    return Result<std::vector<HexCellData>>::success(std::move(cells));
}

[[nodiscard]] Result<std::int32_t> readInt32(const Value& object, const char* key) {
    const Value* field = object.find(key);
    if (field == nullptr || !field->isNumeric())
        return Result<std::int32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              std::string("hex terrain bake is missing numeric '") + key + "'", "hexmap"));
    if (field->isInt64()) return Result<std::int32_t>::success(static_cast<std::int32_t>(field->asInt()));
    return Result<std::int32_t>::success(static_cast<std::int32_t>(field->asDouble()));
}

[[nodiscard]] Result<std::uint32_t> readUInt32(const Value& object, const char* key) {
    auto parsed = readInt32(object, key);
    if (!parsed) return Result<std::uint32_t>::failure(parsed.status());
    if (parsed.value() < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              std::string("hex terrain bake '") + key + "' must not be negative", "hexmap"));
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

}  // namespace

HexTerrainBake snapshotHexTerrain(const HexMap& map) {
    HexTerrainBake bake;
    bake.kind       = HexTerrainBake::Kind::Planar;
    bake.cellCountX = map.cellCountX();
    bake.cellCountZ = map.cellCountZ();
    bake.cellCount  = map.cellCount();
    bake.seed       = map.seed();
    bake.cells.resize(static_cast<std::size_t>(bake.cellCount));
    for (std::int32_t index = 0; index < bake.cellCount; ++index) {
        const HexCellData* cell = map.cellAt(index);
        if (cell != nullptr) bake.cells[static_cast<std::size_t>(index)] = *cell;
    }
    return bake;
}

HexTerrainBake snapshotHexSphereTerrain(const HexSphereMap& map) {
    HexTerrainBake bake;
    bake.kind        = HexTerrainBake::Kind::Sphere;
    bake.cellCount   = map.cellCount();
    bake.subdivision = map.subdivision();
    bake.radius      = map.sphereRadius();
    bake.seed        = map.seed();
    bake.cells.resize(static_cast<std::size_t>(bake.cellCount));
    for (std::int32_t cell = 0; cell < bake.cellCount; ++cell) {
        const HexCellData* record = map.cellAt(cell);
        if (record != nullptr) bake.cells[static_cast<std::size_t>(cell)] = *record;
    }
    return bake;
}

Value hexTerrainBakeToValue(const HexTerrainBake& bake) {
    Value payload = Value::object({});
    if (bake.kind == HexTerrainBake::Kind::Sphere) {
        payload.set("kind", Value("hex.sphere"));
        payload.set("subdivision", Value(static_cast<std::int64_t>(bake.subdivision)));
        payload.set("radius", Value(static_cast<double>(bake.radius)));
        payload.set("cellCount", Value(static_cast<std::int64_t>(bake.cellCount)));
    } else {
        payload.set("kind", Value("hex.terrain"));
        payload.set("cellCountX", Value(static_cast<std::int64_t>(bake.cellCountX)));
        payload.set("cellCountZ", Value(static_cast<std::int64_t>(bake.cellCountZ)));
    }
    payload.set("seed", Value(static_cast<std::int64_t>(bake.seed)));
    payload.set("cells", Value(packCells(bake.cells)));
    return payload;
}

Result<HexTerrainBake> hexTerrainBakeFromValue(const Value& value) {
    if (!value.isObject()) return invalidBake("hex terrain bake must be an object");
    const Value* kindField = value.find("kind");
    if (kindField == nullptr || !kindField->isString()) return invalidBake("hex terrain bake is missing string 'kind'");
    const std::string& kind = kindField->asString();

    HexTerrainBake bake;
    if (kind == "hex.sphere") {
        bake.kind        = HexTerrainBake::Kind::Sphere;
        auto subdivision = readInt32(value, "subdivision");
        if (!subdivision) return Result<HexTerrainBake>::failure(subdivision.status());
        bake.subdivision         = subdivision.value();
        const Value* radiusField = value.find("radius");
        if (radiusField == nullptr || !radiusField->isNumeric())
            return invalidBake("hex sphere bake is missing numeric 'radius'");
        bake.radius    = radiusField->isDouble() ? static_cast<float>(radiusField->asDouble())
                                                 : static_cast<float>(radiusField->asInt());
        auto cellCount = readInt32(value, "cellCount");
        if (!cellCount) return Result<HexTerrainBake>::failure(cellCount.status());
        bake.cellCount = cellCount.value();
    } else if (kind == "hex.terrain") {
        bake.kind       = HexTerrainBake::Kind::Planar;
        auto cellCountX = readInt32(value, "cellCountX");
        auto cellCountZ = readInt32(value, "cellCountZ");
        if (!cellCountX) return Result<HexTerrainBake>::failure(cellCountX.status());
        if (!cellCountZ) return Result<HexTerrainBake>::failure(cellCountZ.status());
        bake.cellCountX = cellCountX.value();
        bake.cellCountZ = cellCountZ.value();
        bake.cellCount  = bake.cellCountX * bake.cellCountZ;
    } else {
        return invalidBake("hex terrain bake kind must be 'hex.terrain' or 'hex.sphere'");
    }

    auto seed = readUInt32(value, "seed");
    if (!seed) return Result<HexTerrainBake>::failure(seed.status());
    bake.seed = seed.value();

    const Value* cellsField = value.find("cells");
    if (cellsField == nullptr || !cellsField->isString())
        return invalidBake("hex terrain bake is missing packed 'cells'");
    if (bake.cellCount < 0) return invalidBake("hex terrain bake cell count must not be negative");
    auto cells = unpackCells(cellsField->asString(), static_cast<std::size_t>(bake.cellCount));
    if (!cells) return Result<HexTerrainBake>::failure(cells.status());
    bake.cells = std::move(cells).takeValue();
    return Result<HexTerrainBake>::success(std::move(bake));
}

Result<void> applyHexTerrain(HexMap& map, const HexTerrainBake& bake) {
    if (bake.kind != HexTerrainBake::Kind::Planar)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "applyHexTerrain requires a planar bake", "hexmap"));
    if (bake.cellCountX <= 0 || bake.cellCountZ <= 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "planar bake size must be positive", "hexmap"));
    if (static_cast<std::int32_t>(bake.cells.size()) != bake.cellCountX * bake.cellCountZ)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "planar bake cell count does not match width and height", "hexmap"));

    auto reset = map.reset(bake.cellCountX, bake.cellCountZ, bake.seed);
    if (!reset) return reset;
    for (std::int32_t index = 0; index < map.cellCount(); ++index) {
        const HexCellData& cell    = bake.cells[static_cast<std::size_t>(index)];
        auto               written = map.setCellState(map.coordinatesAt(index), cell.values, cell.flags);
        if (!written) return written;
    }
    map.markAllChunksDirty();
    return Result<void>::success();
}

Result<void> applyHexSphereTerrain(HexSphereMap& map, const HexTerrainBake& bake) {
    if (bake.kind != HexTerrainBake::Kind::Sphere)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "applyHexSphereTerrain requires a sphere bake", "hexmap"));
    if (map.empty())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "applyHexSphereTerrain requires an existing sphere topology", "hexmap"));
    if (bake.cellCount <= 0 || static_cast<std::int32_t>(bake.cells.size()) != bake.cellCount)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "sphere bake cell count is inconsistent", "hexmap"));
    if (map.cellCount() != bake.cellCount || map.subdivision() != bake.subdivision)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "sphere bake does not match the live topology; call newSphere with the same subdivision", "hexmap"));

    for (std::int32_t cell = 0; cell < bake.cellCount; ++cell) {
        const HexCellData& record  = bake.cells[static_cast<std::size_t>(cell)];
        auto               written = map.setCellState(cell, record.values, record.flags);
        if (!written) return written;
    }
    map.markAllDirty();
    return Result<void>::success();
}

}  // namespace eve::hexmap

// HexWaterMesh.cpp
namespace eve::hexmap {
namespace {

/** @brief One pending surface vertex: a world position plus its texture coordinates. */
struct SurfaceVertex {
    HexVec3 position;
    float   u = 0.f;
    float   v = 0.f;
};

/**
 * @brief Whether three positions span no area, so the face would be invisible.
 *
 * The test reuses the right-handed cross product `HexMeshData::finalize()` derives
 * its face normals from.
 */
[[nodiscard]] bool isDegenerate(HexVec3 a, HexVec3 b, HexVec3 c) noexcept {
    const HexVec3 ab = b - a;
    const HexVec3 ac = c - a;
    const HexVec3 normal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    return (normal.x * normal.x + normal.y * normal.y + normal.z * normal.z) <= 1e-8f;
}

/**
 * @brief Appends one triangle, perturbing every position exactly once.
 *
 * `a -> b -> c` must resolve to a `+Y` normal, otherwise the face is culled. The
 * argument order is the reference project's, so a ported `AddTriangle(a, b, c)`
 * becomes `emitTriangle(out, a, b, c)` unchanged.
 *
 * @param out Destination mesh.
 * @param noise Map noise, applied here so no call site can displace a vertex twice.
 */
void emitTriangle(HexMeshData& out, const SurfaceVertex& a, const SurfaceVertex& b, const SurfaceVertex& c,
                  const HexNoise& noise) {
    const HexVec3 pa = noise.perturb(a.position);
    const HexVec3 pb = noise.perturb(b.position);
    const HexVec3 pc = noise.perturb(c.position);
    if (isDegenerate(pa, pb, pc)) return;
    out.addTriangle(out.addVertex(pa, a.u, a.v), out.addVertex(pb, b.u, b.v), out.addVertex(pc, c.u, c.v));
}

/**
 * @brief Appends the quad whose four corners run `r0 -> r1 -> r2 -> r3` around its outline.
 *
 * `HexMeshData::addQuad(a, b, c, d)` splits the ring `a -> b -> d -> c` along `b -> c`,
 * so the corners are re-ordered here and every call site keeps readable outline order.
 * The reference project's `AddQuad(a, b, c, d)` is therefore
 * `emitQuad(out, a, b, d, c, noise)`.
 */
void emitQuad(HexMeshData& out, const SurfaceVertex& r0, const SurfaceVertex& r1, const SurfaceVertex& r2,
              const SurfaceVertex& r3, const HexNoise& noise) {
    emitTriangle(out, r0, r3, r1, noise);
    emitTriangle(out, r1, r3, r2, noise);
}

/** @brief The water surface height of a cell, or `offGridHeight` when it is off-grid. */
[[nodiscard]] float waterHeightOf(const HexMap& map, HexCoordinates coordinates, float offGridHeight) noexcept {
    return map.contains(coordinates) ? HexMetrics::waterSurfaceY(map.waterLevel(coordinates)) : offGridHeight;
}

/**
 * @brief Whether a cell exists and is flooded.
 *
 * Off-grid neighbours are *not* underwater: the reference routes them through the
 * open-water branch so the map border still gets a water triangle.
 */
[[nodiscard]] bool isWater(const HexMap& map, HexCoordinates coordinates) noexcept {
    return map.contains(coordinates) && map.isUnderwater(coordinates);
}

/**
 * @brief Emits the open-water wedge and, where the neighbour is water, its bridge.
 *
 * This is the reference project's `TriangulateOpenWater`: **one** centre triangle for
 * this direction (not a whole-cell fan), a bridge quad towards a water neighbour that
 * the owning chunk owns, and the corner triangle that closes the third cell of the
 * junction.
 *
 * @param map Source map.
 * @param coordinates Cell the wedge belongs to.
 * @param direction Direction of the wedge.
 * @param centre Water-surface centre of `coordinates`.
 * @param noise Map noise.
 * @param out Destination mesh.
 */
void appendOpenWater(const HexMap& map, HexCoordinates coordinates, HexDirection direction, HexVec3 centre,
                     const HexNoise& noise, HexMeshData& out) {
    const SurfaceVertex c1{centre + HexMetrics::firstWaterCorner(direction), 0.f, 0.f};
    const SurfaceVertex c2{centre + HexMetrics::secondWaterCorner(direction), 0.f, 0.f};
    emitTriangle(out, SurfaceVertex{centre, 0.f, 0.f}, c1, c2, noise);

    HexCoordinates neighbour{};
    if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::SE) ||
        !map.getNeighbor(coordinates, direction, neighbour)) {
        // Only the NE, E and SE edges bridge outward: for a water/water boundary the
        // other side would emit the same quad, so exactly one chunk owns it. An
        // off-grid neighbour has nothing to bridge to.
        return;
    }

    const HexVec3       offset = HexMetrics::waterBridge(direction);
    const float         farY   = waterHeightOf(map, neighbour, centre.y);
    const SurfaceVertex e1{{c1.position.x + offset.x, farY, c1.position.z + offset.z}, 0.f, 0.f};
    const SurfaceVertex e2{{c2.position.x + offset.x, farY, c2.position.z + offset.z}, 0.f, 0.f};
    emitQuad(out, c1, c2, e2, e1, noise);

    if (static_cast<std::int32_t>(direction) > static_cast<std::int32_t>(HexDirection::E)) return;
    HexCoordinates nextNeighbour{};
    if (!map.getNeighbor(coordinates, next(direction), nextNeighbour) || !isWater(map, nextNeighbour)) return;

    // The third cell of the junction is water too, so its own wedge stops at the
    // water corner and this triangle fills the gap between the two bridges.
    HexVec3 corner = map.cellGroundPosition(nextNeighbour) + HexMetrics::firstWaterCorner(previous(direction));
    corner.y       = farY;
    emitTriangle(out, c2, e2, SurfaceVertex{corner, 0.f, 0.f}, noise);
}

/**
 * @brief Emits the shoreline wedge and the bank strip of one direction.
 *
 * This is the reference project's `TriangulateWaterShore`. Two details matter for the
 * result:
 *
 * - the wedge is **subdivided into four triangles** along a five-sample water edge, so
 *   the water edge tessellates like the bank it meets; and
 * - the strip is **flat at the water height** - the far edge only carries the
 *   neighbour's solid corner *positions*, never the neighbour's elevation. A strip
 *   that tilts up to the bank instead reads as a bright band tracing every hexagon
 *   edge, which is exactly the seam this port exists to avoid. The bank terrain
 *   simply rises through the flat water, and the far edge's foam parameter marks where.
 *
 * @param map Source map.
 * @param coordinates Flooded cell the strip belongs to.
 * @param direction Direction towards the land neighbour.
 * @param centre Water-surface centre of `coordinates`.
 * @param noise Map noise.
 * @param out Destination mesh.
 */
void appendWaterShore(const HexMap& map, HexCoordinates coordinates, HexDirection direction, HexVec3 centre,
                      const HexNoise& noise, HexMeshData& out) {
    const EdgeVertices near{centre + HexMetrics::firstWaterCorner(direction),
                            centre + HexMetrics::secondWaterCorner(direction)};

    const SurfaceVertex nearSamples[5] = {SurfaceVertex{near.v1, 0.f, 0.f}, SurfaceVertex{near.v2, 0.f, 0.f},
                                          SurfaceVertex{near.v3, 0.f, 0.f}, SurfaceVertex{near.v4, 0.f, 0.f},
                                          SurfaceVertex{near.v5, 0.f, 0.f}};
    const SurfaceVertex wedgeCentre{centre, 0.f, 0.f};
    for (std::int32_t i = 0; i < 4; ++i) {
        emitTriangle(out, wedgeCentre, nearSamples[i], nearSamples[i + 1], noise);
    }

    const HexCoordinates neighbour       = coordinates.step(direction);
    const HexVec3        neighbourGround = map.cellGroundPosition(neighbour);
    const HexVec3        bank{neighbourGround.x, centre.y, neighbourGround.z};
    // Second corner first, so sample `i` of the far edge lines up with sample `i` of
    // the near edge across the strip - the reference orders them the same way.
    const EdgeVertices far{bank + HexMetrics::secondSolidCorner(opposite(direction)),
                           bank + HexMetrics::firstSolidCorner(opposite(direction))};

    const SurfaceVertex farSamples[5] = {SurfaceVertex{far.v1, 1.f, 0.f}, SurfaceVertex{far.v2, 1.f, 0.f},
                                         SurfaceVertex{far.v3, 1.f, 0.f}, SurfaceVertex{far.v4, 1.f, 0.f},
                                         SurfaceVertex{far.v5, 1.f, 0.f}};
    for (std::int32_t i = 0; i < 4; ++i) {
        emitQuad(out, nearSamples[i], nearSamples[i + 1], farSamples[i + 1], farSamples[i], noise);
    }

    // Close the junction with the next cell around: its water edge when it is flooded,
    // its bank corner when it is land. Either way the vertex stays at the water height.
    HexCoordinates nextNeighbour{};
    if (!map.getNeighbor(coordinates, next(direction), nextNeighbour)) return;
    const HexVec3 corner =
        isWater(map, nextNeighbour)
            ? map.cellGroundPosition(nextNeighbour) + HexMetrics::firstWaterCorner(previous(direction))
            : map.cellGroundPosition(nextNeighbour) + HexMetrics::firstSolidCorner(previous(direction));
    HexVec3 capped = corner;
    capped.y       = centre.y;
    emitTriangle(out, nearSamples[4], farSamples[4], SurfaceVertex{capped, 0.f, 0.f}, noise);
}

}  // namespace

void buildWaterMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out) {
    out.clear();
    if (chunkIndex < 0 || chunkIndex >= map.chunkCount()) {
        out.finalize();
        return;
    }

    const HexNoise& noise = map.noise();

    for (std::int32_t row = 0; row < HexMetrics::kChunkSizeZ; ++row) {
        for (std::int32_t column = 0; column < HexMetrics::kChunkSizeX; ++column) {
            const HexCoordinates coordinates = map.chunkCell(chunkIndex, column, row);
            if (map.cell(coordinates) == nullptr || !map.isUnderwater(coordinates)) continue;

            const HexVec3 ground = map.cellGroundPosition(coordinates);
            const HexVec3 centre{ground.x, HexMetrics::waterSurfaceY(map.waterLevel(coordinates)), ground.z};

            // Dispatch per direction, exactly like the reference: a flooded direction
            // gets the open-water wedge, a land direction gets the shore wedge and its
            // strip. Emitting a full-cell fan up front instead would double the centre
            // triangles of every shoreline direction and leave the strip to fight it.
            for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
                const auto     direction = static_cast<HexDirection>(i);
                HexCoordinates neighbour{};
                const bool     hasNeighbour   = map.getNeighbor(coordinates, direction, neighbour);
                const bool     neighbourWater = hasNeighbour && isWater(map, neighbour);
                if (hasNeighbour && !neighbourWater) {
                    appendWaterShore(map, coordinates, direction, centre, noise, out);
                } else {
                    appendOpenWater(map, coordinates, direction, centre, noise, out);
                }
            }
        }
    }

    out.finalize();
}

}  // namespace eve::hexmap
