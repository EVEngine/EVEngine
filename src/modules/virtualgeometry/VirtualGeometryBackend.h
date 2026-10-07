#pragma once

#include "virtualgeometry/VirtualGeometryAsset.h"

#include <cstdint>
#include <vector>

namespace eve::virtualgeometry {

/** @brief VgUniforms public API. */
struct VgUniforms {
    float viewProj[16];
    float model[16];
    float cameraPos[4];
    float params[4];  // x=viewW, y=viewH, z=projScale, w=errorPx
    float frustum[24];  // 6 planes
    float misc[4];      // x=clusterCount
};

/**
 * @brief Opaque backend state for one virtual-geometry renderer. Owned by
 * VirtualGeometryRenderer; allocated/freed by the active backend.
 */
struct VgBackend {
    void *state = nullptr;  // vulkan::VgState*
};

// ---- backend interface (implemented in vulkan/) ----
/** @brief Vg create. */
void vgCreate(VgBackend &be);
/** @brief Vg destroy. */
void vgDestroy(VgBackend &be);

// Upload a built asset (positions/triangles/clusters) to the GPU.
/** @brief Vg upload. */
void vgUpload(VgBackend &be, const VirtualGeometryAsset &asset);
/** @brief Vg upload uniforms. */
void vgUploadUniforms(VgBackend &be, const VgUniforms &u);
/** @brief Vg reset. */
void vgReset(VgBackend &be, int visibleCapacity);

// Dispatch cull + raster for the given workgroup counts. Returns visible count.
/** @brief Vg update. */
int vgUpdate(VgBackend &be, int clusterCount, int visibleCapacity, int viewW, int viewH);

// Read the visibility buffer back to CPU (packed depth<<16|clusterId).
/** @brief Vg read pixels. */
bool vgReadPixels(VgBackend &be, std::vector<uint32_t> &out);

}  // namespace eve::virtualgeometry
