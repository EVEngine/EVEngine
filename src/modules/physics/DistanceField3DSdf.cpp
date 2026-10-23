#include "physics/DistanceField3DSdf.h"

#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <cmath>
#include <limits>

namespace eve::physics {
namespace {
Status invalid(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "physics.sdf.field3d"));
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

void DistanceField3DSdf::bind(const DistanceField3D& field) { field_ = &field; }
void DistanceField3DSdf::clear() { field_ = nullptr; }

float DistanceField3DSdf::sample(float x, float y, float z) const {
    if (!field_) return std::numeric_limits<float>::infinity();
    try {
        return field_->sample(x, y, z);
    } catch (const eve::Exception&) {
        return std::numeric_limits<float>::infinity();
    }
}

SdfField3DGradientStatus DistanceField3DSdf::gradient(float x, float y, float z, float& outNx, float& outNy,
                                                      float& outNz) const {
    if (!field_) return SdfField3DGradientStatus::Undefined;
    return centralGradient(*this, x, y, z, field_->getCellSize() * 0.5f, outNx, outNy, outNz);
}

unsigned DistanceField3DSdf::revision() const { return field_ ? field_->getRevision() : 0u; }

void DistanceField3DSdf::bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const {
    if (!field_) {
        minX = minY = minZ = maxX = maxY = maxZ = 0.f;
        return;
    }
    minX = field_->getOriginX();
    minY = field_->getOriginY();
    minZ = field_->getOriginZ();
    maxX = minX + float(field_->getWidth() - 1) * field_->getCellSize();
    maxY = minY + float(field_->getHeight() - 1) * field_->getCellSize();
    maxZ = minZ + float(field_->getDepth() - 1) * field_->getCellSize();
}

Result<SdfField3DBlob> sdfField3DBlobFromDistanceField(const DistanceField3D& field) {
    SdfField3DBlob blob;
    blob.kind            = SdfField3DSampleKind::VertexGrid;
    blob.width           = field.getWidth();
    blob.height          = field.getHeight();
    blob.depth           = field.getDepth();
    blob.cellSize        = field.getCellSize();
    blob.originX         = field.getOriginX();
    blob.originY         = field.getOriginY();
    blob.originZ         = field.getOriginZ();
    blob.outsideDistance = field.getOutsideDistance();
    blob.distances.resize(size_t(field.getSampleCount()));
    for (int z = 0; z < blob.depth; ++z)
        for (int y = 0; y < blob.height; ++y)
            for (int x = 0; x < blob.width; ++x)
                blob.distances[size_t(x + blob.width * (y + blob.height * z))] = field.getDistance(x, y, z);
    auto valid = validateSdfField3DBlob(blob);
    if (!valid) return Result<SdfField3DBlob>::failure(valid.status());
    return Result<SdfField3DBlob>::success(std::move(blob));
}

Result<DistanceField3D> distanceFieldFromSdfField3DBlob(const SdfField3DBlob& blob) {
    auto valid = validateSdfField3DBlob(blob);
    if (!valid) return Result<DistanceField3D>::failure(valid.status());
    if (blob.kind != SdfField3DSampleKind::VertexGrid)
        return Result<DistanceField3D>::failure(invalid("Only VertexGrid blobs import into DistanceField3D"));
    try {
        DistanceField3D field(blob.width, blob.height, blob.depth, blob.cellSize, blob.originX, blob.originY,
                              blob.originZ, blob.outsideDistance);
        field.setDistances(blob.distances);
        return Result<DistanceField3D>::success(std::move(field));
    } catch (const eve::Exception& error) {
        return Result<DistanceField3D>::failure(
            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "physics.sdf.field3d")));
    }
}

}  // namespace eve::physics
