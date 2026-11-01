#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace eve::graphics::raytracing::path {

// Private upload ABI shared with pt_scene.glsl and pt_path.rgen. Explicit
// alignment preserves std430 array strides with both aligned and packed GLM.
inline constexpr uint32_t invalidIndex = 0xffffffffu;

struct alignas(16) Vertex {
    glm::vec4 position{0.f};
    glm::vec4 normal{0.f};
    glm::vec4 texcoord{0.f};
    glm::vec4 tangent{0.f};
    glm::vec4 bitangent{0.f};
};

struct alignas(16) Triangle {
    glm::uvec4 indicesMaterial{0u};
    glm::uvec4 emitter{invalidIndex, 0u, 0u, 0u};
};

struct alignas(16) Instance {
    glm::mat4  objectToWorld{1.f};
    glm::mat4  normalToWorld{1.f};
    glm::uvec4 offsets{0u};
};

struct alignas(16) Material {
    glm::vec4  baseColor{1.f};
    glm::vec4  emissionRoughness{0.f, 0.f, 0.f, 1.f};
    glm::vec4  parameters{0.f};
    glm::uvec4 textures{invalidIndex};
};

struct alignas(16) TextureRecord {
    glm::uvec4 image{0u};
    glm::vec4  scaleOffset{1.f, 1.f, 0.f, 0.f};
    glm::vec4  parameters{1.f, 0.f, 0.f, 0.f};
};

struct alignas(16) Light {
    glm::vec4 p0Area{0.f};
    glm::vec4 p1Kind{0.f};
    glm::vec4 p2{0.f};
    glm::vec4 emission{0.f};
};

struct alignas(16) Constants {
    glm::mat4  invViewProj{1.f};
    glm::vec4  eye{0.f};
    glm::uvec4 sampling{0u, 1u, 4u, 0u};
    glm::vec4  environment{0.f};
    glm::uvec4 counts{0u};
};

struct alignas(16) TemporalConstants {
    glm::mat4  previousViewProj{1.f};
    glm::vec4  previousEye{0.f};
    glm::vec4  currentEye{0.f};
    glm::vec4  parameters{0.f};
    glm::uvec4 counts{0u};
};
static_assert(sizeof(TemporalConstants) == 128 && offsetof(TemporalConstants, previousEye) == 64 &&
              offsetof(TemporalConstants, currentEye) == 80 && offsetof(TemporalConstants, parameters) == 96 &&
              offsetof(TemporalConstants, counts) == 112);

static_assert(std::is_trivially_copyable_v<Vertex> && std::is_standard_layout_v<Vertex>);
static_assert(sizeof(Vertex) == 80 && offsetof(Vertex, normal) == 16 && offsetof(Vertex, texcoord) == 32 &&
              offsetof(Vertex, tangent) == 48 && offsetof(Vertex, bitangent) == 64);
static_assert(std::is_trivially_copyable_v<Triangle> && std::is_standard_layout_v<Triangle>);
static_assert(sizeof(Triangle) == 32 && offsetof(Triangle, emitter) == 16);
static_assert(std::is_trivially_copyable_v<Instance> && std::is_standard_layout_v<Instance>);
static_assert(sizeof(Instance) == 144 && offsetof(Instance, normalToWorld) == 64 && offsetof(Instance, offsets) == 128);
static_assert(std::is_trivially_copyable_v<Material> && std::is_standard_layout_v<Material>);
static_assert(sizeof(Material) == 64 && offsetof(Material, emissionRoughness) == 16 &&
              offsetof(Material, parameters) == 32 && offsetof(Material, textures) == 48);
static_assert(std::is_trivially_copyable_v<TextureRecord> && std::is_standard_layout_v<TextureRecord>);
static_assert(sizeof(TextureRecord) == 48 && offsetof(TextureRecord, scaleOffset) == 16 &&
              offsetof(TextureRecord, parameters) == 32);
static_assert(std::is_trivially_copyable_v<Light> && std::is_standard_layout_v<Light>);
static_assert(sizeof(Light) == 64 && offsetof(Light, p1Kind) == 16 && offsetof(Light, p2) == 32 &&
              offsetof(Light, emission) == 48);
static_assert(std::is_trivially_copyable_v<Constants> && std::is_standard_layout_v<Constants>);
static_assert(sizeof(Constants) == 128 && offsetof(Constants, eye) == 64 && offsetof(Constants, sampling) == 80 &&
              offsetof(Constants, environment) == 96 && offsetof(Constants, counts) == 112);

}  // namespace eve::graphics::raytracing::path
