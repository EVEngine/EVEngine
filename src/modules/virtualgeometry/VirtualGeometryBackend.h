#pragma once

#include "virtualgeometry/VirtualGeometryAsset.h"

#include <cstdint>
#include <vector>

namespace eve::virtualgeometry {

struct VgUniforms {
    float viewProj[16];
    float model[16];
    float cameraPos[4];
    float params[4];  // x=viewW, y=viewH, z=projScale, w=errorPx
    float frustum[24];  // 6 planes
    float misc[4];      // x=clusterCount
};

/**
 * Opaque backend state for one virtual-geometry renderer. Owned by
 * VirtualGeometryRenderer; allocated/freed by the active backend.
 */
struct VgBackend {
    void *state = nullptr;  // vulkan::VgState*
};

// ---- backend interface (implemented in vulkan/) ----
void vgCreate(VgBackend &be);
void vgDestroy(VgBackend &be);

// Upload a built asset (positions/triangles/clusters) to the GPU.
void vgUpload(VgBackend &be, const VirtualGeometryAsset &asset);
void vgUploadUniforms(VgBackend &be, const VgUniforms &u);
void vgReset(VgBackend &be, int visibleCapacity);

// Dispatch cull + raster for the given workgroup counts. Returns visible count.
int vgUpdate(VgBackend &be, int clusterCount, int visibleCapacity, int viewW, int viewH);

// Read the visibility buffer back to CPU (packed depth<<16|clusterId).
bool vgReadPixels(VgBackend &be, std::vector<uint32_t> &out);

}  // namespace eve::virtualgeometry
