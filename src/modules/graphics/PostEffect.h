#pragma once

#include <cstdint>

namespace eve::graphics {

/** @brief Stable insertion points for full-screen effects. */
enum class PostEffectStage { AfterOpaque, BeforeTransparent, BeforeTonemap, AfterTonemap };

enum PostEffectInput : uint32_t {
    PostInputColor    = 1u << 0u,
    PostInputDepth    = 1u << 1u,
    PostInputNormal   = 1u << 2u,
    PostInputMotion   = 1u << 3u,
    PostInputObjectId = 1u << 4u,
};

/** @brief Backend-neutral scheduling contract supplied by post-effect modules. */
struct PostEffectDesc {
    PostEffectStage stage           = PostEffectStage::AfterTonemap;
    uint32_t        inputs          = PostInputColor;
    int             priority        = 0;
    float           resolutionScale = 1.f;
};

inline const char* postEffectStageName(PostEffectStage stage) {
    switch (stage) {
        case PostEffectStage::AfterOpaque: return "afterOpaque";
        case PostEffectStage::BeforeTransparent: return "beforeTransparent";
        case PostEffectStage::BeforeTonemap: return "beforeTonemap";
        case PostEffectStage::AfterTonemap: return "afterTonemap";
    }
    return "afterTonemap";
}

}  // namespace eve::graphics
