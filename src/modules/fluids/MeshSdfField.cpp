#include "fluids/MeshSdfField.h"

#include "common/Diagnostic.h"
#include "physics/DistanceField3DSdf.h"

#include <cmath>
#include <limits>

namespace eve::fluids {
namespace {
Status invalid(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.sdf.field3d"));
}

SdfField3DGradientStatus centralGradient(const ISdfField3D& field, float x, float y, float z, float epsilon,
                                         float& outNx, float& outNy, float& outNz) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(epsilon > 0.f))
        return SdfField3DGradientStatus::Undefined;
    const float dx  = field.sample(x + epsilon, y, z) - field.sample(x - epsilon, y, z);
    const float dy  = field.sample(x, y + epsilon, z) - field.sample(x, y - epsilon, z);
    const float dz  = field.sample(x, y, z + epsilon) - field.sample(x, y, z - epsilon);
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(len) || len <= 1e-8f) return SdfField3DGradientStatus::Undefined;
    outNx = dx / len;
    outNy = dy / len;
    outNz = dz / len;
    return SdfField3DGradientStatus::Defined;
}
}  // namespace

void MeshSdfField::bind(const MeshSdf& field) {
    field_ = &field;
    ++revision_;
}
void MeshSdfField::clear() {
    field_ = nullptr;
    ++revision_;
}

float MeshSdfField::sample(float x, float y, float z) const {
    if (!field_) return std::numeric_limits<float>::infinity();
    return field_->sample(glm::vec3(x, y, z));
}

SdfField3DGradientStatus MeshSdfField::gradient(float x, float y, float z, float& outNx, float& outNy,
                                                float& outNz) const {
    if (!field_ || field_->cellSize <= 0.f) return SdfField3DGradientStatus::Undefined;
    const auto  g   = field_->gradient(glm::vec3(x, y, z));
    const float len = glm::length(g);
    if (!std::isfinite(len) || len <= 1e-8f)
        return centralGradient(*this, x, y, z, field_->cellSize * 0.5f, outNx, outNy, outNz);
    outNx = g.x / len;
    outNy = g.y / len;
    outNz = g.z / len;
    return SdfField3DGradientStatus::Defined;
}

unsigned MeshSdfField::revision() const { return revision_; }

void MeshSdfField::bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const {
    if (!field_ || field_->dims.x < 1 || field_->dims.y < 1 || field_->dims.z < 1) {
        minX = minY = minZ = maxX = maxY = maxZ = 0.f;
        return;
    }
    minX = field_->origin.x;
    minY = field_->origin.y;
    minZ = field_->origin.z;
    maxX = minX + float(field_->dims.x - 1) * field_->cellSize;
    maxY = minY + float(field_->dims.y - 1) * field_->cellSize;
    maxZ = minZ + float(field_->dims.z - 1) * field_->cellSize;
}

Result<SdfField3DBlob> sdfField3DBlobFromMeshSdf(const MeshSdf& sdf, float outsideDistance) {
    if (sdf.dims.x < 2 || sdf.dims.y < 2 || sdf.dims.z < 2 || !(sdf.cellSize > 0.f) || !std::isfinite(sdf.cellSize) ||
        !std::isfinite(sdf.origin.x) || !std::isfinite(sdf.origin.y) || !std::isfinite(sdf.origin.z) ||
        !std::isfinite(outsideDistance) || size_t(sdf.voxelCount()) != sdf.distances.size())
        return Result<SdfField3DBlob>::failure(invalid("Invalid MeshSdf for SDF blob export"));
    SdfField3DBlob blob;
    blob.kind            = SdfField3DSampleKind::VertexGrid;
    blob.width           = sdf.dims.x;
    blob.height          = sdf.dims.y;
    blob.depth           = sdf.dims.z;
    blob.cellSize        = sdf.cellSize;
    blob.originX         = sdf.origin.x;
    blob.originY         = sdf.origin.y;
    blob.originZ         = sdf.origin.z;
    blob.outsideDistance = outsideDistance;
    blob.distances       = sdf.distances;
    auto valid           = validateSdfField3DBlob(blob);
    if (!valid) return Result<SdfField3DBlob>::failure(valid.status());
    return Result<SdfField3DBlob>::success(std::move(blob));
}

Result<MeshSdf> meshSdfFromSdfField3DBlob(const SdfField3DBlob& blob) {
    auto valid = validateSdfField3DBlob(blob);
    if (!valid) return Result<MeshSdf>::failure(valid.status());
    if (blob.kind != SdfField3DSampleKind::VertexGrid)
        return Result<MeshSdf>::failure(invalid("Only VertexGrid blobs import into MeshSdf"));
    MeshSdf sdf;
    sdf.dims      = {blob.width, blob.height, blob.depth};
    sdf.cellSize  = blob.cellSize;
    sdf.origin    = {blob.originX, blob.originY, blob.originZ};
    sdf.distances = blob.distances;
    return Result<MeshSdf>::success(std::move(sdf));
}

Result<physics::DistanceField3D> distanceFieldFromMeshSdf(const MeshSdf& sdf, float outsideDistance) {
    auto blob = sdfField3DBlobFromMeshSdf(sdf, outsideDistance);
    if (!blob) return Result<physics::DistanceField3D>::failure(blob.status());
    return physics::distanceFieldFromSdfField3DBlob(blob.value());
}

Result<MeshSdf> meshSdfFromDistanceField(const physics::DistanceField3D& field) {
    auto blob = physics::sdfField3DBlobFromDistanceField(field);
    if (!blob) return Result<MeshSdf>::failure(blob.status());
    return meshSdfFromSdfField3DBlob(blob.value());
}

}  // namespace eve::fluids
