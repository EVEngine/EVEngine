#pragma once

#include "virtualgeometry/VirtualGeometryAsset.h"
#include "virtualgeometry/VirtualGeometryBackend.h"
#include "virtualgeometry/Builder.h"

#include <cstdint>
#include <vector>

namespace eve::data {
class ByteData;
}

namespace eve::virtualgeometry {

/**
 * @brief A virtual-geometry renderer: preprocesses a mesh into a cluster DAG, uploads
 * it to the GPU, then each frame runs GPU-driven culling + a software rasterizer
 * into a visibility buffer.
 *
 * Lifecycle (Squirrel):
 *   r <- vg.newRenderer()
 *   r.buildIcosphere(4)            (or build(...) with raw arrays)
 *   r.setViewport(w, h, fovYDeg, errorPx)
 *   r.setCamera(view[16], proj[16], model[16], cam[3])
 *   visible <- r.update()
 *   ok <- r.resolve(rgbaArray, w, h)   // CPU readback of the visibility buffer
 */
class EVENGINE_API_DOMAINS VirtualGeometryRenderer {
public:
    /** @brief Virtual geometry renderer. */
    VirtualGeometryRenderer();
    /** @brief Virtual geometry renderer. */
    ~VirtualGeometryRenderer();

    VirtualGeometryRenderer(const VirtualGeometryRenderer &) = delete;
    VirtualGeometryRenderer &operator=(const VirtualGeometryRenderer &) = delete;

    /** @brief True when ready. */
    bool isReady() const { return backend_.state != nullptr; }

    // ---- build (preprocess + upload) ----
    /** @brief Builds . */
    bool build(const float *positions, int vertexCount, const std::uint32_t *indices, int indexCount);
    /** @brief Builds . */
    bool build(const VirtualGeometryBuilder::MeshInput &in);
    /** @brief Convenience: procedural unit icosphere, `subdiv` subdivision levels. */
    bool buildIcosphere(int subdiv);

    // ---- per-frame ----
    /** @brief Sets the viewport. */
    void setViewport(int width, int height, float fovYDeg, float errorPx = 1.0f);
    /** @brief Sets the camera. */
    void setCamera(const float view[16], const float proj[16], const float model[16],
                   const float camPos[3]);
    /** @brief Script-friendly: identity view (camera looking down -Z) + perspective. */
    void setCameraSimple(float camX, float camY, float camZ, float nearZ = 0.1f, float farZ = 100.f);
    /** @brief Spin the virtualized model about Y by `yaw` radians each frame. */
    void setModelYaw(float yaw);
    /** @brief Run cull + raster; returns the number of visible clusters. */
    int update();
    /** Resolve the visibility buffer to RGBA (w*h*4 bytes). */
    /** @brief Resolve. */
    bool resolve(unsigned char *outRgba, int &outW, int &outH);
    /** @brief Resolve into a heap ByteData (RGBA) for scripting/display. */
    eve::data::ByteData *resolveByteData();
    /** @brief Returns the view width. */
    int getViewWidth() const { return width_; }
    /** @brief Returns the view height. */
    int getViewHeight() const { return height_; }

    // ---- stats ----
    /** @brief Returns the cluster count. */
    int getClusterCount() const;
    /** @brief Returns the visible count. */
    int getVisibleCount() const { return lastVisible_; }
    /** @brief Returns the total triangle count. */
    int getTotalTriangleCount() const;
    /** @brief Returns the lod level. */
    int getLodLevel(int clusterId) const;
    /** @brief Returns the max lod level. */
    int getMaxLodLevel() const;

private:
    void buildIcosphereInternal(int subdiv);
    void updateUniforms();

    VgBackend backend_;
    VirtualGeometryAsset asset_;
    VirtualGeometryBuilder::Options builderOptions_{};
    VgUniforms uniforms_{};
    int width_ = 1, height_ = 1;
    int lastVisible_ = 0;
    int visibleCapacity_ = 4096;
    float modelYaw_ = 0.f;
};

}  // namespace eve::virtualgeometry
