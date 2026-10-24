#include "common/SdfField3D.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <limits>

namespace eve {
namespace {
Status invalidBlob(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "eve.sdf.field3d"));
}

bool finiteScalar(float value) { return std::isfinite(value); }
}  // namespace

Result<void> validateSdfField3DBlob(const SdfField3DBlob& blob) {
    if (blob.kind != SdfField3DSampleKind::VertexGrid)
        return Result<void>::failure(invalidBlob("Unsupported SDF sample kind"));
    if (blob.width < 2 || blob.height < 2 || blob.depth < 2 || blob.width > 512 || blob.height > 512 ||
        blob.depth > 512)
        return Result<void>::failure(invalidBlob("SDF grid dimensions must be in [2,512]"));
    if (!finiteScalar(blob.cellSize) || blob.cellSize <= 0.f || !finiteScalar(blob.originX) ||
        !finiteScalar(blob.originY) || !finiteScalar(blob.originZ) || !finiteScalar(blob.outsideDistance))
        return Result<void>::failure(invalidBlob("SDF grid metadata must be finite with positive cellSize"));
    const uint64_t samples = uint64_t(blob.width) * uint64_t(blob.height) * uint64_t(blob.depth);
    if (samples > 4000000ull || samples != blob.distances.size())
        return Result<void>::failure(invalidBlob("SDF sample count mismatch or exceeds 4M budget"));
    for (float distance : blob.distances)
        if (!finiteScalar(distance) || std::abs(distance) > 10000.f)
            return Result<void>::failure(invalidBlob("SDF samples must be finite within ±10000"));
    return Result<void>::success();
}

Result<Value> encodeSdfField3DBlob(const SdfField3DBlob& blob) {
    auto valid = validateSdfField3DBlob(blob);
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Array distances;
    distances.reserve(blob.distances.size());
    for (float distance : blob.distances) distances.push_back(double(distance));
    return Result<Value>::success(
        Value::Object{{"schemaId", std::string(SdfField3DBlob::schemaId)},
                      {"schemaVersion", std::int64_t(SdfField3DBlob::schemaVersion)},
                      {"kind", std::int64_t(blob.kind)},
                      {"width", std::int64_t(blob.width)},
                      {"height", std::int64_t(blob.height)},
                      {"depth", std::int64_t(blob.depth)},
                      {"cellSize", double(blob.cellSize)},
                      {"origin", Value::Array{double(blob.originX), double(blob.originY), double(blob.originZ)}},
                      {"outsideDistance", double(blob.outsideDistance)},
                      {"distances", Value(std::move(distances))}});
}

Result<SdfField3DBlob> decodeSdfField3DBlob(const Value& value) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return Result<SdfField3DBlob>::failure(invalidBlob("SDF blob must be an object"));
    for (const char* key : {"schemaId", "schemaVersion", "kind", "width", "height", "depth", "cellSize", "origin",
                            "outsideDistance", "distances"})
        if (!object->contains(key))
            return Result<SdfField3DBlob>::failure(invalidBlob("SDF blob missing required field"));
    const auto* schema  = object->at("schemaId").getIf<std::string>();
    const auto* version = object->at("schemaVersion").getIf<std::int64_t>();
    const auto* kind    = object->at("kind").getIf<std::int64_t>();
    const auto* width   = object->at("width").getIf<std::int64_t>();
    const auto* height  = object->at("height").getIf<std::int64_t>();
    const auto* depth   = object->at("depth").getIf<std::int64_t>();
    const auto* origin  = object->at("origin").getIf<Value::Array>();
    const auto* samples = object->at("distances").getIf<Value::Array>();
    auto        asFloat = [](const Value& entry) -> float {
        if (auto real = entry.getIf<double>()) return float(*real);
        if (auto integer = entry.getIf<std::int64_t>()) return float(*integer);
        return std::numeric_limits<float>::quiet_NaN();
    };
    if (!schema || *schema != SdfField3DBlob::schemaId || !version || *version != SdfField3DBlob::schemaVersion ||
        !kind || !width || !height || !depth || !origin || origin->size() != 3 || !samples)
        return Result<SdfField3DBlob>::failure(invalidBlob("SDF blob schema or dimensions are invalid"));
    SdfField3DBlob blob;
    blob.kind            = static_cast<SdfField3DSampleKind>(*kind);
    blob.width           = int(*width);
    blob.height          = int(*height);
    blob.depth           = int(*depth);
    blob.cellSize        = asFloat(object->at("cellSize"));
    blob.originX         = asFloat((*origin)[0]);
    blob.originY         = asFloat((*origin)[1]);
    blob.originZ         = asFloat((*origin)[2]);
    blob.outsideDistance = asFloat(object->at("outsideDistance"));
    blob.distances.reserve(samples->size());
    for (const auto& sample : *samples) blob.distances.push_back(asFloat(sample));
    auto valid = validateSdfField3DBlob(blob);
    if (!valid) return Result<SdfField3DBlob>::failure(valid.status());
    return Result<SdfField3DBlob>::success(std::move(blob));
}

}  // namespace eve
