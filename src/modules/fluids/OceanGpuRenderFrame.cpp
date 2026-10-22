#include "fluids/OceanGpuSpectrum.h"
#include "fluids/OceanRendererInternal.h"
#include "fluids/OceanWake.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include <bit>
#include <cmath>
#include <exception>

namespace eve::fluids {
OceanGpuRenderFrameBuilder OceanRenderer::gpuFrameBuilder(const OceanGpuSpectrum&   spectrum,
                                                          const OceanGpuFrameToken& token) {
    return {*this, spectrum, token};
}
Result<void> OceanGpuRenderFrameBuilder::publish() const {
    auto& state = *renderer_.impl_;
    auto  fail  = [](DiagnosticCode code, const char* message) {
        return Result<void>::failure(Diagnostic::error(code, message, "fluids.ocean.render"));
    };
    if (state.lifetime.expired()) return fail(DiagnosticCode::StaleHandle, "Graphics retired");
    if (state.backend != OceanRenderBackend::GpuResident)
        return fail(DiagnosticCode::Conflict, "Renderer expects a CPU reference projection");
    auto finite = [](double value) { return std::isfinite(value) && std::abs(value) <= 1e9; };
    if (!finite(center.x) || !finite(center.y) || !finite(origin.x) || !finite(origin.y) || !finite(origin.z))
        return fail(DiagnosticCode::InvalidArgument, "Invalid ocean render coordinates");
    auto atlas = spectrum_.atlas(token_);
    if (!atlas) return Result<void>::failure(atlas.status());
    const auto&           view = atlas.value();
    std::array<float, 32> parameters{};
    for (unsigned i = 0; i < view.bandCount; ++i) {
        const auto& band      = view.bands[i];
        parameters[i * 5]     = static_cast<float>(band.resolution);
        parameters[i * 5 + 1] = static_cast<float>(band.length);
        parameters[i * 5 + 2] = static_cast<float>(band.firstRow);
        parameters[i * 5 + 3] = static_cast<float>(std::remainder(center.x, band.length));
        parameters[i * 5 + 4] = static_cast<float>(std::remainder(center.y, band.length));
    }
    parameters[20]                     = static_cast<float>(view.bandCount);
    parameters[21]                     = static_cast<float>(center.x - origin.x);
    parameters[22]                     = static_cast<float>(view.seaLevel - origin.y);
    parameters[23]                     = static_cast<float>(center.y - origin.z);
    unsigned                  wakeSize = 1;
    std::vector<std::uint8_t> wakePixels(8, 0);
    if (wake) {
        const auto grid = wake->gridView();
        if (std::abs(grid.time - token_.time()) > 1e-8)
            return fail(DiagnosticCode::InvalidArgument, "Ocean GPU and wake timestamps differ");
        wakeSize         = grid.settings.resolution;
        const auto count = wakeSize * wakeSize;
        wakePixels.resize(count * 8);
        for (unsigned i = 0; i < count * 2; ++i) {
            const auto value = i < count ? grid.height[i] : grid.foam[i - count];
            const auto bits  = std::bit_cast<std::uint32_t>(static_cast<float>(value));
            for (unsigned channel = 0; channel < 4; ++channel)
                wakePixels[i * 4 + channel] = static_cast<std::uint8_t>((bits >> (8 * channel)) & 255U);
        }
        parameters[24] = static_cast<float>(wakeSize);
        parameters[25] = static_cast<float>(grid.settings.length);
        parameters[26] = static_cast<float>(grid.settings.absorptionWidth);
        parameters[27] = static_cast<float>(center.x - grid.settings.center.x);
        parameters[28] = static_cast<float>(center.y - grid.settings.center.y);
    }
    // Write inactive textures only. No partial upload can become the visible ocean frame.
    const auto back = 1U - state.front;
    try {
        auto resize = [&](graphics::Texture*& texture, unsigned width, unsigned height) -> Result<void> {
            if (texture && texture->getWidth() == static_cast<int>(width) &&
                texture->getHeight() == static_cast<int>(height))
                return Result<void>::success();
            graphics::TextureCreateInfo info;
            info.sampler = graphics::TextureSampler::nearest();
            std::vector<std::uint8_t> zero(width * height * 4, 0);
            auto*                     replacement =
                state.graphics->newTexture(static_cast<int>(width), static_cast<int>(height), zero.data(), info);
            if (!replacement) return fail(DiagnosticCode::Unsupported, "Ocean data texture unavailable");
            if (texture && state.graphics->releaseTexture(texture)) delete texture;
            texture = replacement;
            return Result<void>::success();
        };
        auto resized = resize(state.atlas[back], view.width, view.height);
        if (!resized) return resized;
        resized = resize(state.wake[back], wakeSize, wakeSize * 2);
        if (!resized) return resized;
        auto copied = state.graphics->updateTextureFromResidentRgba8(
            state.atlas[back], view.buffer, static_cast<int>(view.width), static_cast<int>(view.height));
        if (!copied) return copied;
        auto uploaded = state.graphics->updateTextureRegion(state.wake[back], 0, 0, static_cast<int>(wakeSize),
                                                            static_cast<int>(wakeSize * 2), wakePixels);
        if (!uploaded) return uploaded;
        auto sent = state.shader->sendFloatBlock(parameters);
        if (!sent) return sent;
    } catch (const std::exception& error) {
        return fail(DiagnosticCode::Failed, error.what());
    }
    state.origin    = origin;
    state.front     = back;
    state.published = true;
    return Result<void>::success();
}
}  // namespace eve::fluids
