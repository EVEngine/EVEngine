#include "virtualgeometry/VirtualGeometryBackend.h"

namespace eve::virtualgeometry {

void vgCreate(VgBackend &be) { be.state = nullptr; }

void vgDestroy(VgBackend &be) { be.state = nullptr; }

void vgUpload(VgBackend &, const VirtualGeometryAsset &) {}

void vgUploadUniforms(VgBackend &, const VgUniforms &) {}

void vgReset(VgBackend &, int) {}

int vgUpdate(VgBackend &, int, int, int, int) { return 0; }

bool vgReadPixels(VgBackend &, std::vector<uint32_t> &) { return false; }

}  // namespace eve::virtualgeometry
