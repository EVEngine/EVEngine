#include "physics/rope/Rope3DBakeCapture.h"
#include "physics/backend/bake/DeformCaptureInternal.h"
#include "physics/rope/Rope3D.h"
namespace eve::physics {
Result<DeformFrame> captureDeformFrame(const Rope3D& source, float seconds) {
    return bake_detail::captureDeformSource(source, seconds, DeformPrimitive::Segments,
                                            [&source] { return source.captureSegmentIndices(); });
}
}  // namespace eve::physics
