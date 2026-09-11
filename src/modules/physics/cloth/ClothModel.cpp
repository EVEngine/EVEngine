#include "physics/cloth/ClothModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <tuple>

namespace eve::physics {
namespace {

int64_t edgeKey(int a, int b) {
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    return (int64_t(lo) << 32) | int64_t(static_cast<uint32_t>(hi));
}

eve::Result<ClothModel> invalidModel(const char* message) {
    return eve::Result<ClothModel>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "physics.clothModel"));
}

template <typename T>
eve::Result<T> decodeFailure(eve::DiagnosticCode code, std::string message, std::string path = "physics.clothModel") {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

bool hasExactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> expected) {
    if (object.size() != expected.size()) return false;
    for (std::string_view name : expected)
        if (!object.contains(std::string(name))) return false;
    return true;
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool readFiniteFloat(const eve::Value& value, float& output) {
    if (!value.isNumeric()) return false;
    const double number = value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(number);
    return true;
}

constexpr size_t kMaximumDecodedParticles = 1'000'000;

}  // namespace

eve::Result<ClothModel> ClothModel::fromTriangles(std::span<const float> positions,
                                                  std::span<const int>   triangleIndices,
                                                  std::span<const float> inverseMasses) {
    if (positions.empty() || positions.size() % 3 != 0)
        return invalidModel("cloth positions must be a non-empty packed XYZ array");
    if (triangleIndices.empty() || triangleIndices.size() % 3 != 0)
        return invalidModel("cloth indices must contain complete triangles");

    const size_t particleCount = positions.size() / 3;
    if (!inverseMasses.empty() && inverseMasses.size() != particleCount)
        return invalidModel("cloth inverse-mass count must match the particle count");

    ClothModel candidate;
    candidate.particles_.reserve(particleCount);
    for (size_t i = 0; i < particleCount; ++i) {
        const float inverseMass = inverseMasses.empty() ? 1.f : inverseMasses[i];
        if (!std::isfinite(positions[i * 3]) || !std::isfinite(positions[i * 3 + 1]) ||
            !std::isfinite(positions[i * 3 + 2]) || !std::isfinite(inverseMass) || inverseMass < 0.f)
            return invalidModel("cloth particles require finite positions and non-negative inverse masses");
        candidate.particles_.push_back({positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2], inverseMass});
    }

    std::map<int64_t, std::vector<int>> edgeTriangles;
    std::set<int64_t>                   structuralEdges;
    for (size_t i = 0; i < triangleIndices.size(); i += 3) {
        const int a = triangleIndices[i];
        const int b = triangleIndices[i + 1];
        const int c = triangleIndices[i + 2];
        if (a < 0 || b < 0 || c < 0 || static_cast<size_t>(a) >= particleCount ||
            static_cast<size_t>(b) >= particleCount || static_cast<size_t>(c) >= particleCount)
            return invalidModel("cloth triangle index is out of range");
        if (a == b || b == c || c == a) return invalidModel("cloth triangles must reference three distinct particles");

        const auto& pa  = candidate.particles_[static_cast<size_t>(a)];
        const auto& pb  = candidate.particles_[static_cast<size_t>(b)];
        const auto& pc  = candidate.particles_[static_cast<size_t>(c)];
        const float abx = pb.x - pa.x, aby = pb.y - pa.y, abz = pb.z - pa.z;
        const float acx = pc.x - pa.x, acy = pc.y - pa.y, acz = pc.z - pa.z;
        const float nx = aby * acz - abz * acy;
        const float ny = abz * acx - abx * acz;
        const float nz = abx * acy - aby * acx;
        if (nx * nx + ny * ny + nz * nz <= 1e-12f) return invalidModel("cloth triangles must have non-zero rest area");

        const int triangle = static_cast<int>(candidate.triangles_.size());
        candidate.triangles_.push_back({{a, b, c}});
        const int edges[3][2] = {{a, b}, {b, c}, {c, a}};
        for (const auto& edge : edges) {
            const int64_t key = edgeKey(edge[0], edge[1]);
            edgeTriangles[key].push_back(triangle);
            structuralEdges.insert(key);
        }
    }

    auto addDistance = [&](int a, int b, ClothConstraintKind kind) {
        const auto& pa = candidate.particles_[static_cast<size_t>(a)];
        const auto& pb = candidate.particles_[static_cast<size_t>(b)];
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        candidate.distanceConstraints_.push_back({a, b, std::sqrt(dx * dx + dy * dy + dz * dz), kind});
    };
    for (int64_t key : structuralEdges) {
        addDistance(static_cast<int>(key >> 32), static_cast<int>(static_cast<uint32_t>(key)),
                    ClothConstraintKind::Structural);
    }

    std::set<int64_t> bendEdges;
    for (const auto& entry : edgeTriangles) {
        if (entry.second.size() > 2)
            return invalidModel("cloth topology must be manifold: an edge has more than two triangles");
        if (entry.second.size() != 2) continue;
        const auto& first     = candidate.triangles_[static_cast<size_t>(entry.second[0])];
        const auto& second    = candidate.triangles_[static_cast<size_t>(entry.second[1])];
        const int   edgeA     = static_cast<int>(entry.first >> 32);
        const int   edgeB     = static_cast<int>(static_cast<uint32_t>(entry.first));
        int         oppositeA = -1;
        int         oppositeB = -1;
        for (int vertex : first.vertices)
            if (vertex != edgeA && vertex != edgeB) oppositeA = vertex;
        for (int vertex : second.vertices)
            if (vertex != edgeA && vertex != edgeB) oppositeB = vertex;
        if (oppositeA < 0 || oppositeB < 0 || oppositeA == oppositeB)
            return invalidModel("cloth adjacent triangles have invalid topology");
        candidate.foldConstraints_.push_back({edgeA, edgeB, oppositeA, oppositeB});
        if (bendEdges.insert(edgeKey(oppositeA, oppositeB)).second)
            addDistance(oppositeA, oppositeB, ClothConstraintKind::Bend);
    }

    candidate.closed_ = std::all_of(edgeTriangles.begin(), edgeTriangles.end(),
                                    [](const auto& entry) { return entry.second.size() == 2; });
    if (candidate.closed_) {
        double signedVolume = 0.0;
        for (const ClothModelTriangle& triangle : candidate.triangles_) {
            const auto& a = candidate.particles_[static_cast<size_t>(triangle.vertices[0])];
            const auto& b = candidate.particles_[static_cast<size_t>(triangle.vertices[1])];
            const auto& c = candidate.particles_[static_cast<size_t>(triangle.vertices[2])];
            signedVolume += (double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) +
                             double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) +
                             double(a.z) * (double(b.x) * c.y - double(b.y) * c.x)) /
                            6.0;
        }
        if (std::abs(signedVolume) <= 1e-9) return invalidModel("closed cloth topology must have non-zero rest volume");
        candidate.restVolume_ = static_cast<float>(signedVolume);
    }

    struct Neighbor {
        int   particle = 0;
        float distance = 0.f;
    };
    std::vector<std::vector<Neighbor>> adjacency(particleCount);
    for (int64_t key : structuralEdges) {
        const int   a  = static_cast<int>(key >> 32);
        const int   b  = static_cast<int>(static_cast<uint32_t>(key));
        const auto& pa = candidate.particles_[static_cast<size_t>(a)];
        const auto& pb = candidate.particles_[static_cast<size_t>(b)];
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        adjacency[static_cast<size_t>(a)].push_back({b, distance});
        adjacency[static_cast<size_t>(b)].push_back({a, distance});
    }
    using QueueEntry = std::tuple<float, int, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;
    std::vector<float> tetherDistance(particleCount, std::numeric_limits<float>::infinity());
    std::vector<int>   tetherAnchor(particleCount, -1);
    for (size_t particle = 0; particle < particleCount; ++particle) {
        if (candidate.particles_[particle].inverseMass != 0.f) continue;
        tetherDistance[particle] = 0.f;
        tetherAnchor[particle]   = static_cast<int>(particle);
        queue.emplace(0.f, static_cast<int>(particle), static_cast<int>(particle));
    }
    while (!queue.empty()) {
        const auto [distance, anchor, particle] = queue.top();
        queue.pop();
        if (distance > tetherDistance[static_cast<size_t>(particle)] + 1e-6f ||
            anchor != tetherAnchor[static_cast<size_t>(particle)])
            continue;
        for (const Neighbor& neighbor : adjacency[static_cast<size_t>(particle)]) {
            const float  nextDistance = distance + neighbor.distance;
            const size_t next         = static_cast<size_t>(neighbor.particle);
            if (nextDistance + 1e-6f < tetherDistance[next] ||
                (std::abs(nextDistance - tetherDistance[next]) <= 1e-6f &&
                 (tetherAnchor[next] < 0 || anchor < tetherAnchor[next]))) {
                tetherDistance[next] = nextDistance;
                tetherAnchor[next]   = anchor;
                queue.emplace(nextDistance, anchor, neighbor.particle);
            }
        }
    }
    for (size_t particle = 0; particle < particleCount; ++particle)
        if (candidate.particles_[particle].inverseMass > 0.f && tetherAnchor[particle] >= 0)
            candidate.tetherConstraints_.push_back(
                {static_cast<int>(particle), tetherAnchor[particle], tetherDistance[particle]});

    return eve::Result<ClothModel>::success(std::move(candidate));
}

eve::Result<ClothModel> ClothModel::grid(int cols, int rows, float spacing, float originX, float originY,
                                         float originZ) {
    if (cols < 2 || rows < 2) return invalidModel("cloth grid dimensions must be at least two");
    if (!std::isfinite(spacing) || spacing <= 0.f)
        return invalidModel("cloth grid spacing must be finite and positive");
    if (!std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ))
        return invalidModel("cloth grid origin must be finite");
    if (static_cast<size_t>(cols) > static_cast<size_t>(std::numeric_limits<int>::max()) / static_cast<size_t>(rows))
        return invalidModel("cloth grid particle count exceeds the supported index range");

    std::vector<float> positions;
    std::vector<float> inverseMasses;
    std::vector<int>   indices;
    positions.reserve(static_cast<size_t>(cols * rows * 3));
    inverseMasses.reserve(static_cast<size_t>(cols * rows));
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            positions.push_back(originX + static_cast<float>(col) * spacing);
            positions.push_back(originY);
            positions.push_back(originZ + static_cast<float>(row) * spacing);
            inverseMasses.push_back(row == 0 ? 0.f : 1.f);
        }
    }
    for (int row = 0; row + 1 < rows; ++row) {
        for (int col = 0; col + 1 < cols; ++col) {
            const int a = row * cols + col;
            const int b = a + 1;
            const int d = (row + 1) * cols + col;
            const int c = d + 1;
            indices.insert(indices.end(), {a, d, b, d, c, b});
        }
    }
    auto result = fromTriangles(positions, indices, inverseMasses);
    if (!result) return result;
    ClothModel model = std::move(result).takeValue();

    // Preserve the established grid behavior and solver order exactly: PBD
    // projection order affects finite-iteration results. Arbitrary meshes use
    // the canonical edge order produced by fromTriangles above.
    model.distanceConstraints_.clear();
    auto addGridDistance = [&](int a, int b, ClothConstraintKind kind) {
        const auto& pa = model.particles_[static_cast<size_t>(a)];
        const auto& pb = model.particles_[static_cast<size_t>(b)];
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        model.distanceConstraints_.push_back({a, b, std::sqrt(dx * dx + dy * dy + dz * dz), kind});
    };
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int particle = row * cols + col;
            if (col + 1 < cols) addGridDistance(particle, particle + 1, ClothConstraintKind::Structural);
            if (row + 1 < rows) addGridDistance(particle, particle + cols, ClothConstraintKind::Structural);
            if (col + 1 < cols && row + 1 < rows)
                addGridDistance(particle, particle + cols + 1, ClothConstraintKind::Shear);
            if (col > 0 && row + 1 < rows) addGridDistance(particle, particle + cols - 1, ClothConstraintKind::Shear);
            if (col + 2 < cols) addGridDistance(particle, particle + 2, ClothConstraintKind::Bend);
            if (row + 2 < rows) addGridDistance(particle, particle + cols * 2, ClothConstraintKind::Bend);
        }
    }
    model.gridCols_    = cols;
    model.gridRows_    = rows;
    model.gridSpacing_ = spacing;
    return eve::Result<ClothModel>::success(std::move(model));
}

eve::Value ClothModel::toValue() const {
    eve::Value::Array positions;
    eve::Value::Array inverseMasses;
    eve::Value::Array triangles;
    positions.reserve(particles_.size() * 3);
    inverseMasses.reserve(particles_.size());
    triangles.reserve(triangles_.size() * 3);
    for (const ClothModelParticle& particle : particles_) {
        positions.emplace_back(particle.x);
        positions.emplace_back(particle.y);
        positions.emplace_back(particle.z);
        inverseMasses.emplace_back(particle.inverseMass);
    }
    for (const ClothModelTriangle& triangle : triangles_) {
        triangles.emplace_back(triangle.vertices[0]);
        triangles.emplace_back(triangle.vertices[1]);
        triangles.emplace_back(triangle.vertices[2]);
    }

    eve::Value gridMetadata;
    if (gridCols_ > 0 && gridRows_ > 0) {
        gridMetadata = eve::Value::Object{
            {"cols", eve::Value(gridCols_)}, {"rows", eve::Value(gridRows_)}, {"spacing", eve::Value(gridSpacing_)}};
    }
    return eve::Value::Object{{"grid", std::move(gridMetadata)},
                              {"inverseMasses", eve::Value(std::move(inverseMasses))},
                              {"positions", eve::Value(std::move(positions))},
                              {"schema", eve::Value("eve.cloth-model")},
                              {"schemaVersion", eve::Value(static_cast<int>(SchemaVersion))},
                              {"triangles", eve::Value(std::move(triangles))}};
}

eve::Result<std::string> ClothModel::toJson() const { return toValue().toJson(); }

eve::Result<ClothModel> ClothModel::fromJson(std::string_view json) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<ClothModel>::failure(value.status());
    return fromValue(value.value());
}

eve::Result<ClothModel> ClothModel::fromValue(const eve::Value& value) {
    const auto* root = value.getIf<eve::Value::Object>();
    if (!root || !hasExactFields(*root, {"schema", "schemaVersion", "positions", "inverseMasses", "triangles", "grid"}))
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError,
                                         "cloth model root has missing or unknown fields");

    const eve::Value* schema  = field(*root, "schema");
    const eve::Value* version = field(*root, "schemaVersion");
    if (!schema || !schema->isString() || schema->asString() != "eve.cloth-model")
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError, "cloth model schema must be eve.cloth-model",
                                         "schema");
    if (!version || !version->isInt64())
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError,
                                         "cloth model schemaVersion must be an integer", "schemaVersion");
    if (version->asInt() != SchemaVersion)
        return decodeFailure<ClothModel>(eve::DiagnosticCode::UnknownVersion,
                                         "cloth model schema version is not supported", "schemaVersion");

    const auto* encodedPositions = field(*root, "positions")->getIf<eve::Value::Array>();
    const auto* encodedMasses    = field(*root, "inverseMasses")->getIf<eve::Value::Array>();
    const auto* encodedTriangles = field(*root, "triangles")->getIf<eve::Value::Array>();
    if (!encodedPositions || encodedPositions->empty() || encodedPositions->size() % 3 != 0 ||
        encodedPositions->size() / 3 > kMaximumDecodedParticles || !encodedMasses ||
        encodedMasses->size() != encodedPositions->size() / 3 || !encodedTriangles || encodedTriangles->empty() ||
        encodedTriangles->size() % 3 != 0)
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError, "cloth model arrays have invalid sizes");

    std::vector<float> positions;
    std::vector<float> inverseMasses;
    std::vector<int>   triangles;
    positions.reserve(encodedPositions->size());
    inverseMasses.reserve(encodedMasses->size());
    triangles.reserve(encodedTriangles->size());
    for (const eve::Value& encoded : *encodedPositions) {
        float decoded = 0.f;
        if (!readFiniteFloat(encoded, decoded))
            return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError, "cloth position must be a finite float",
                                             "positions");
        positions.push_back(decoded);
    }
    for (const eve::Value& encoded : *encodedMasses) {
        float decoded = 0.f;
        if (!readFiniteFloat(encoded, decoded) || decoded < 0.f)
            return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError,
                                             "cloth inverse mass must be finite and non-negative", "inverseMasses");
        inverseMasses.push_back(decoded);
    }
    for (const eve::Value& encoded : *encodedTriangles) {
        if (!encoded.isInt64() || encoded.asInt() < 0 || encoded.asInt() > std::numeric_limits<int>::max())
            return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError,
                                             "cloth triangle index must be a non-negative integer", "triangles");
        triangles.push_back(static_cast<int>(encoded.asInt()));
    }

    auto decoded = fromTriangles(positions, triangles, inverseMasses);
    if (!decoded) return decoded;

    const eve::Value* gridValue = field(*root, "grid");
    if (gridValue->isNull()) return decoded;
    const auto* gridObject = gridValue->getIf<eve::Value::Object>();
    if (!gridObject || !hasExactFields(*gridObject, {"cols", "rows", "spacing"}))
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError,
                                         "cloth grid metadata has missing or unknown fields", "grid");
    const eve::Value* colsValue    = field(*gridObject, "cols");
    const eve::Value* rowsValue    = field(*gridObject, "rows");
    const eve::Value* spacingValue = field(*gridObject, "spacing");
    float             spacing      = 0.f;
    if (!colsValue || !colsValue->isInt64() || !rowsValue || !rowsValue->isInt64() || colsValue->asInt() < 2 ||
        colsValue->asInt() > std::numeric_limits<int>::max() || rowsValue->asInt() < 2 ||
        rowsValue->asInt() > std::numeric_limits<int>::max() || !spacingValue ||
        !readFiniteFloat(*spacingValue, spacing) || spacing <= 0.f)
        return decodeFailure<ClothModel>(eve::DiagnosticCode::ParseError, "cloth grid metadata values are invalid",
                                         "grid");

    const int cols      = static_cast<int>(colsValue->asInt());
    const int rows      = static_cast<int>(rowsValue->asInt());
    auto      canonical = grid(cols, rows, spacing, positions[0], positions[1], positions[2]);
    if (!canonical) return eve::Result<ClothModel>::failure(canonical.status());
    if (canonical.value().particles().size() != decoded.value().particles().size() ||
        canonical.value().triangles().size() != decoded.value().triangles().size())
        return decodeFailure<ClothModel>(eve::DiagnosticCode::Conflict,
                                         "cloth grid metadata disagrees with its topology", "grid");
    for (size_t i = 0; i < canonical.value().particles().size(); ++i) {
        const auto& expected = canonical.value().particles()[i];
        const auto& actual   = decoded.value().particles()[i];
        if (std::abs(expected.x - actual.x) > 1e-5f || std::abs(expected.y - actual.y) > 1e-5f ||
            std::abs(expected.z - actual.z) > 1e-5f || expected.inverseMass != actual.inverseMass)
            return decodeFailure<ClothModel>(eve::DiagnosticCode::Conflict,
                                             "cloth grid metadata disagrees with its particles", "grid");
    }
    for (size_t i = 0; i < canonical.value().triangles().size(); ++i)
        for (int vertex = 0; vertex < 3; ++vertex)
            if (canonical.value().triangles()[i].vertices[vertex] != decoded.value().triangles()[i].vertices[vertex])
                return decodeFailure<ClothModel>(eve::DiagnosticCode::Conflict,
                                                 "cloth grid metadata disagrees with triangle ordering", "grid");
    return canonical;
}

}  // namespace eve::physics
