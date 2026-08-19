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
class VirtualGeometryRenderer {
public:
    VirtualGeometryRenderer();
    ~VirtualGeometryRenderer();

    VirtualGeometryRenderer(const VirtualGeometryRenderer &) = delete;
    VirtualGeometryRenderer &operator=(const VirtualGeometryRenderer &) = delete;

    bool isReady() const { return backend_.state != nullptr; }

    // ---- build (preprocess + upload) ----
    bool build(const float *positions, int vertexCount, const std::uint32_t *indices, int indexCount);
    bool build(const VirtualGeometryBuilder::MeshInput &in);
    /** @brief Convenience: procedural unit icosphere, `subdiv` subdivision levels. */
    bool buildIcosphere(int subdiv);

    // ---- per-frame ----
    void setViewport(int width, int height, float fovYDeg, float errorPx = 1.0f);
    void setCamera(const float view[16], const float proj[16], const float model[16],
                   const float camPos[3]);
    /** @brief Script-friendly: identity view (camera looking down -Z) + perspective. */
    void setCameraSimple(float camX, float camY, float camZ, float nearZ = 0.1f, float farZ = 100.f);
    /** @brief Spin the virtualized model about Y by `yaw` radians each frame. */
    void setModelYaw(float yaw);
    /** @brief Run cull + raster; returns the number of visible clusters. */
    int update();
    /** Resolve the visibility buffer to RGBA (w*h*4 bytes). */
    bool resolve(unsigned char *outRgba, int &outW, int &outH);
    /** @brief Resolve into a heap ByteData (RGBA) for scripting/display. */
    eve::data::ByteData *resolveByteData();
    int getViewWidth() const { return width_; }
    int getViewHeight() const { return height_; }

    // ---- stats ----
    int getClusterCount() const;
    int getVisibleCount() const { return lastVisible_; }
    int getTotalTriangleCount() const;
    int getLodLevel(int clusterId) const;
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
