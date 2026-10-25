#pragma once
#include <array>
#include "fluids/OceanRenderer.h"
namespace eve::graphics {
class Mesh;
class Shader;
class Texture;
}  // namespace eve::graphics
namespace eve::fluids {
struct OceanRenderer::Impl {
    graphics::Graphics*               graphics = nullptr;
    std::weak_ptr<const void>         lifetime;
    graphics::Mesh*                   mesh   = nullptr;
    graphics::Shader*                 shader = nullptr;
    OceanMeshData                     topology;
    OceanRenderBackend                backend = OceanRenderBackend::CpuReference;
    std::array<graphics::Texture*, 2> atlas{}, wake{}, hullMask{};
    unsigned                          hullFront     = 0;
    bool                              hullPublished = false;
    glm::dvec3                        origin{0.0}, hullOrigin{0.0};
    unsigned                          front     = 0;
    bool                              published = false;
    ~Impl();
};
}  // namespace eve::fluids
