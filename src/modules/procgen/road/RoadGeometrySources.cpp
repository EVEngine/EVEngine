#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include "common/Value.h"
#include "procgen/MeshBuild.h"
#include "procgen/road/RoadGeometrySourcesInternal.h"
namespace eve::procgen::road {
namespace {
constexpr const char* key       = "road.geometrySources";
constexpr const char* schema    = "eve.procgen.road.geometrySources";
constexpr const char* algorithm = "fnv1a64-le-noncrypto-v1";
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, key));
}
bool geometryValid(const MeshBuild& m) {
    const auto vertices = m.positions().size() / 3;
    if (vertices == 0 || m.positions().size() % 3 || m.indices().empty() || m.indices().size() % 3 ||
        m.positions().size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        m.indices().size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        m.normals().size() != vertices * 3 || m.uvs().size() != vertices * 2 ||
        (!m.colors().empty() && m.colors().size() != vertices * 4) ||
        m.triangleGroups().size() != m.indices().size() / 3)
        return false;
    for (const auto* values : {&m.positions(), &m.normals(), &m.uvs(), &m.colors()})
        for (float v : *values)
            if (!std::isfinite(v)) return false;
    for (auto i : m.indices())
        if (i >= vertices) return false;
    for (auto g : m.triangleGroups())
        if (g < 0 || static_cast<std::size_t>(g) >= m.groupNames().size()) return false;
    return true;
}
std::string digest(const MeshBuild& m) {
    std::uint64_t hash = 14695981039346656037ull;
    auto          byte = [&](std::uint8_t b) {
        hash ^= b;
        hash *= 1099511628211ull;
    };
    auto integer = [&](std::uint64_t v) {
        for (int n = 0; n < 8; ++n) byte(static_cast<std::uint8_t>(v >> (8 * n)));
    };
    for (const auto* values : {&m.positions(), &m.normals(), &m.uvs(), &m.colors()}) {
        integer(values->size());
        for (float v : *values) integer(std::bit_cast<std::uint32_t>(v));
    }
    integer(m.indices().size());
    for (auto i : m.indices()) integer(i);
    integer(m.triangleGroups().size());
    for (auto g : m.triangleGroups()) integer(static_cast<std::uint32_t>(g));
    integer(m.groupNames().size());
    for (const auto& name : m.groupNames()) {
        integer(name.size());
        for (unsigned char b : name) byte(b);
    }
    std::array<char, 16> out{};
    constexpr char       digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < out.size(); ++i) out[15 - i] = digits[(hash >> (4 * i)) & 15u];
    return std::string(out.data(), out.size());
}
bool partitionValid(std::span<const RoadGeometrySourceSpan> spans, std::size_t triangles) {
    std::size_t next = 0;
    for (const auto& s : spans) {
        if (static_cast<unsigned>(s.kind) > 2 ||
            (s.kind == RoadGeometrySourceKind::NetworkDerived ? s.id != 0 : s.id == 0) || s.firstTriangle != next ||
            s.triangleCount == 0 || s.triangleCount > triangles - next)
            return false;
        next += s.triangleCount;
    }
    return next == triangles;
}
const std::int64_t* number(const eve::Value& v, const char* field) {
    const auto* p = v.find(field);
    return p ? p->getIf<std::int64_t>() : nullptr;
}
const std::string* text(const eve::Value& v, const char* field) {
    const auto* p = v.find(field);
    return p ? p->getIf<std::string>() : nullptr;
}
}  // namespace
PreparedRoadGeometrySources::PreparedRoadGeometrySources(std::vector<RoadGeometrySourceSpan> spans)
    : spans_(std::move(spans)) {}
PreparedRoadGeometrySources::~PreparedRoadGeometrySources()                                                 = default;
PreparedRoadGeometrySources::PreparedRoadGeometrySources(PreparedRoadGeometrySources&&) noexcept            = default;
PreparedRoadGeometrySources& PreparedRoadGeometrySources::operator=(PreparedRoadGeometrySources&&) noexcept = default;
std::span<const RoadGeometrySourceSpan> PreparedRoadGeometrySources::spans() const noexcept { return spans_; }
Result<PreparedRoadGeometrySources>     PreparedRoadGeometrySources::prepare(const MeshBuild& mesh) {
    auto encoded = mesh.getMeta(key, "");
    if (encoded.empty())
        return failure<PreparedRoadGeometrySources>(DiagnosticCode::Unsupported, "mesh has no road source attribution");
    if (encoded.size() > 64u * 1024u * 1024u || !geometryValid(mesh))
        return failure<PreparedRoadGeometrySources>(DiagnosticCode::InvalidArgument,
                                                        "invalid attributed mesh layout or metadata budget");
    auto decoded = eve::Value::fromJson(encoded);
    if (!decoded.ok()) return Result<PreparedRoadGeometrySources>::failure(decoded.status());
    const auto& root = decoded.value();
    auto        s = text(root, "schema"), a = text(root, "algorithm"), d = text(root, "digest");
    auto        version = number(root, "version"), count = number(root, "triangles");
    const auto* rows  = root.find("spans");
    const auto* array = rows ? rows->getIf<eve::Value::Array>() : nullptr;
    if (!s || *s != schema || !a || *a != algorithm || !d || !version || *version != 1 || !count ||
        *count != mesh.getIndexCount() / 3 || !array || array->size() > mesh.indices().size() / 3)
        return failure<PreparedRoadGeometrySources>(DiagnosticCode::InvalidArgument,
                                                        "unsupported or malformed road source schema");
    if (*d != digest(mesh))
        return failure<PreparedRoadGeometrySources>(DiagnosticCode::StaleHandle,
                                                        "road source metadata does not match mesh streams");
    std::vector<RoadGeometrySourceSpan> spans;
    spans.reserve(array->size());
    for (const auto& row : *array) {
        auto kind = number(row, "kind"), id = number(row, "id"), first = number(row, "first"),
             size = number(row, "count");
        if (!kind || *kind < 0 || *kind > 2 || !id || *id < 0 || *id > std::numeric_limits<std::uint32_t>::max() ||
            !first || *first < 0 || *first > *count || !size || *size <= 0 || *size > *count)
            return failure<PreparedRoadGeometrySources>(DiagnosticCode::InvalidArgument,
                                                            "malformed road source interval");
        spans.push_back({static_cast<RoadGeometrySourceKind>(*kind), static_cast<std::uint32_t>(*id),
                             static_cast<std::uint32_t>(*first), static_cast<std::uint32_t>(*size)});
    }
    if (!partitionValid(spans, mesh.indices().size() / 3))
        return failure<PreparedRoadGeometrySources>(DiagnosticCode::InvalidArgument,
                                                        "road source intervals do not partition the mesh");
    return Result<PreparedRoadGeometrySources>::success(PreparedRoadGeometrySources(std::move(spans)));
}
namespace detail {
Result<void> sealRoadGeometrySources(MeshBuild& mesh, std::span<const RoadGeometrySourceSpan> spans) {
    if (!geometryValid(mesh) || !partitionValid(spans, mesh.indices().size() / 3))
        return failure<void>(DiagnosticCode::InvariantViolation, "producer road source partition is invalid");
    eve::Value::Array rows;
    rows.reserve(spans.size());
    for (const auto& s : spans)
        rows.emplace_back(eve::Value::Object{{"kind", static_cast<int>(s.kind)},
                                             {"id", static_cast<std::int64_t>(s.id)},
                                             {"first", static_cast<std::int64_t>(s.firstTriangle)},
                                             {"count", static_cast<std::int64_t>(s.triangleCount)}});
    eve::Value root    = eve::Value::Object{{"schema", schema},
                                            {"version", 1},
                                            {"algorithm", algorithm},
                                            {"digest", digest(mesh)},
                                            {"triangles", mesh.getIndexCount() / 3},
                                            {"spans", std::move(rows)}};
    auto       encoded = root.toJson();
    if (!encoded.ok()) return Result<void>::failure(encoded.status());
    if (encoded.value().size() > 64u * 1024u * 1024u)
        return failure<void>(DiagnosticCode::InvalidArgument, "road source metadata exceeds budget");
    mesh.setMeta(key, encoded.value());
    return Result<void>::success();
}
}  // namespace detail
}  // namespace eve::procgen::road
