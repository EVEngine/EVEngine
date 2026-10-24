#include <bit>
#include <exception>
#include "fluids/OceanHullMask.h"
#include "fluids/OceanRendererInternal.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"

namespace eve::fluids {
OceanHullMaskPublication OceanRenderer::hullMaskBuilder(const OceanHullMaskFrame& frame) { return {*this, frame}; }
Result<void>             OceanHullMaskPublication::publish() const {
    auto& state = *renderer_.impl_;
    auto  fail  = [](DiagnosticCode code, const char* message) {
        return Result<void>::failure(Diagnostic::error(code, message, "fluids.ocean.hull"));
    };
    if (state.lifetime.expired()) return fail(DiagnosticCode::StaleHandle, "Graphics retired");
    if (frame_.size() == 0) {
        state.hullPublished = false;
        return Result<void>::success();
    }
    std::vector<std::uint8_t> pixels(4 * 545 * 4, 0);
    auto                      put = [&](std::size_t scalar, float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned channel = 0; channel < 4; ++channel)
            pixels[scalar * 4 + channel] = static_cast<std::uint8_t>((bits >> (8 * channel)) & 255U);
    };
    put(0, static_cast<float>(frame_.size()));
    for (std::size_t row = 0; row < frame_.rows().size(); ++row)
        for (unsigned col = 0; col < 4; ++col) put((row + 1) * 4 + col, frame_.rows()[row][col]);
    const unsigned back = 1 - state.hullFront;
    try {
        if (!state.hullMask[back]) {
            graphics::TextureCreateInfo info;
            info.sampler         = graphics::TextureSampler::nearest();
            state.hullMask[back] = state.graphics->newTexture(4, 545, pixels.data(), info);
            if (!state.hullMask[back]) return fail(DiagnosticCode::Unsupported, "Hull mask texture unavailable");
        } else {
            auto result = state.graphics->updateTextureRegion(state.hullMask[back], 0, 0, 4, 545, pixels);
            if (!result) return result;
        }
    } catch (const std::exception& error) {
        return fail(DiagnosticCode::Failed, error.what());
    }
    state.hullFront     = back;
    state.hullOrigin    = frame_.origin();
    state.hullPublished = true;
    return Result<void>::success();
}
}  // namespace eve::fluids
