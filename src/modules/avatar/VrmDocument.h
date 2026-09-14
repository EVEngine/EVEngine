#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>
#include "common/Result.h"

namespace eve::avatar {

/** @brief Owning, validated VRM import data; independent of parser and source bytes. */
struct VrmTexture {
    int                  image = -1;
    std::array<float, 2> offset{0, 0}, scale{1, 1};
    float                rotation = 0;
    int                  wrapS = 10497, wrapT = 10497;
};
/** @brief Linear material factors and image references from VRMC_materials_mtoon. */
struct VrmMaterial {
    std::string          name, alphaMode = "OPAQUE", outlineMode = "none";
    bool                 mtoon = false, doubleSided = false, zWrite = false;
    int                  queue = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 3> shade{0, 0, 0}, emission{}, matcap{1, 1, 1}, rim{}, outline{};
    float                cutoff = .5f, shift = 0, shiftTextureScale = 1, toony = .9f, gi = .9f;
    float                rimMix = 1, rimPower = 1, rimLift = 0, outlineWidth = 0, outlineMix = 1;
    float                normalScale = 1, emissionStrength = 1, scrollX = 0, scrollY = 0, rotationSpeed = 0;
    // base, shade, normal, shift, emission, matcap, rim, outline width, UV mask.
    std::array<VrmTexture, 9> textures;
};
/** @brief One expression's indexed morph/material/UV operations. */
struct VrmExpression {
    struct Morph {
        int   node = -1, index = -1;
        float weight = 0;
    };
    struct Color {
        int                  material = -1;
        std::string          type;
        std::array<float, 4> target{};
    };
    struct Uv {
        int                  material = -1;
        std::array<float, 2> scale{1, 1}, offset{};
    };
    std::string        name, overrideBlink = "none", overrideLookAt = "none", overrideMouth = "none";
    bool               binary = false;
    std::vector<Morph> morphs;
    std::vector<Color> colors;
    std::vector<Uv>    uvs;
};
/** @brief VRM gaze maps; angles are degrees, expression outputs are weights. */
struct VrmLookAt {
    struct Range {
        float input = 90, output = 10;
    };
    bool                 present = false, expression = false;
    std::array<float, 3> offset{};
    Range                inner, outer, down, up;
};
/** @brief Node-local sphere/capsule used only by referencing spring groups. */
struct VrmCollider {
    int                  node = -1;
    std::array<float, 3> offset{}, tail{};
    float                radius  = 0;
    bool                 capsule = false;
};
/** @brief Ordered joints and explicit collision membership of one VRM spring. */
struct VrmSpring {
    struct Joint {
        int                  node   = -1;
        float                radius = 0, stiffness = 1, gravityPower = 0, drag = .5f;
        std::array<float, 3> gravity{0, -1, 0};
    };
    std::string        name;
    int                center = -1;
    std::vector<Joint> joints;
    std::vector<int>   colliders;
};
/** @brief Validated VRM schema data. Unknown optional fields are ignored; unknown versions fail. */
struct VrmDocument {
    std::string                         version;
    std::vector<std::string>            nodes;
    std::vector<int>                    parents, nodeMeshes;
    std::vector<std::vector<int>>       meshMaterials;
    std::map<std::string, int>          humanoid;
    std::vector<VrmExpression>          expressions;
    VrmLookAt                           look;
    std::vector<VrmCollider>            colliders;
    std::vector<VrmSpring>              springs;
    std::vector<VrmMaterial>            materials;
    std::vector<std::vector<std::byte>> images;
};

/**
 * @brief Validate a self-contained GLB's VRM extension before publishing runtime state.
 * @param bytes Borrowed for this call only; result owns all imported data.
 * @return Owning document or ParseError/UnknownVersion/Unsupported diagnostics.
 * @note Thread independent; no callbacks, filesystem access, or observable mutation.
 */
[[nodiscard]] eve::Result<VrmDocument> parseVrm(std::span<const std::byte> bytes);

}  // namespace eve::avatar
