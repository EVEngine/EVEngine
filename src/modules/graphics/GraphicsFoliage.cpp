#include <array>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include "common/Subscription.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "image/ImageData.h"

// GraphicsFoliage.cpp
namespace eve::graphics {
Result<void> Graphics::configureFoliageWind(Material* material, float directionX, float directionZ, float strength,
                                            float bending, float bendingSpeed, float branch, float branchSpeed,
                                            float flutter, float flutterSpeed, float fadeDistance, float bendFactor) {
    if (!material)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "null foliage material", {}, {},
                                                       "graphics.foliage.wind"));
    if (!foliageWindMotionTexture_) {
        std::array<uint16_t, 9 * 4> neutral{};
        for (size_t layer = 0; layer < 9; ++layer) {
            neutral[layer * 4]     = 0x3c00;  // 1.0
            neutral[layer * 4 + 2] = 0x3800;  // 0.5
        }
        auto uploaded = newTextureArrayRgba16f(1, 1, 9, neutral);
        if (!uploaded) return Result<void>::failure(uploaded.status());
        foliageWindMotionTexture_ = uploaded.value();
    }
    if (!foliageWindNoiseTexture_) {
        image::ImageData noise(4, 4, "RGBA8");
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const float value = float((x * 73 + y * 151 + x * y * 29 + 37) & 255) / 255.f;
                noise.setPixel(x, y, image::ImageData::Colorf{value, 1.f - value, .5f, 1.f});
            }
        }
        foliageWindNoiseTexture_ = newTextureFromImageData(&noise, true, true);
        if (!foliageWindNoiseTexture_)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "foliage noise upload failed", {},
                                                           {}, "graphics.foliage.wind"));
    }
    return material->setFoliageWind(foliageWindMotionTexture_, foliageWindNoiseTexture_, directionX, directionZ,
                                    strength, bending, bendingSpeed, branch, branchSpeed, flutter, flutterSpeed,
                                    fadeDistance, bendFactor);
}
}  // namespace eve::graphics

// GraphicsLifetime.cpp
namespace eve::graphics {
namespace {
struct Lifetime {
    eve::Observer<> retiring;
    bool            retired = false;
};
bool registryAlive = false;
struct Registry {
    std::unordered_map<const Graphics*, std::shared_ptr<Lifetime>> tokens;
    Registry() { registryAlive = true; }
    ~Registry() { registryAlive = false; }
};
auto& lifetimes() {
    static Registry registry;
    return registry.tokens;
}
auto lifetime(const Graphics* graphics) {
    auto& value = lifetimes()[graphics];
    if (!value) value = std::make_shared<Lifetime>();
    return value;
}
}  // namespace
std::weak_ptr<const void> Graphics::resourceLifetime() const {
    auto value = lifetime(this);
    return value->retired ? std::weak_ptr<const void>{} : value;
}
Result<eve::Subscription> Graphics::onResourcesRetiring(std::function<void()> callback) const {
    auto value = lifetime(this);
    if (value->retired)
        return Result<eve::Subscription>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Graphics resources are retiring"));
    return Result<eve::Subscription>::success(value->retiring.subscribe(std::move(callback)));
}
void Graphics::retireResourceLifetime() const {
    if (!registryAlive) return;
    auto it = lifetimes().find(this);
    if (it == lifetimes().end() || it->second->retired) return;
    auto value          = it->second;
    value->retired      = true;
    const auto failures = value->retiring.notifyChecked([]() noexcept {});
    if (failures) std::fprintf(stderr, "Graphics resource retirement: %zu callback failures\n", failures);
    lifetimes().erase(this);
}
}  // namespace eve::graphics
