#include "physics/cloth/Cloth3DBakeCapture.h"
#include "physics/backend/bake/DeformCaptureInternal.h"
#include "physics/cloth/Cloth3D.h"
namespace eve::physics {
Result<DeformFrame> captureDeformFrame(const Cloth3D& source, float seconds) {
    return bake_detail::captureDeformSource(source, seconds, DeformPrimitive::Triangles,
                                            [&source] { return source.captureTriangleIndices(); });
}
}  // namespace eve::physics
