#include "procgen/texture/EverydayMaterials.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "procgen/texture/EverydayPatterns.h"

namespace eve::procgen {
namespace {
struct Profile {
    Rgba8 color;
    float roughness, metallic, relief;
};
constexpr std::array<Profile, 13> profiles{{{{178, 132, 80}, .44f, 0.f, .16f},
                                            {{180, 183, 188}, .28f, 1.f, .03f},
                                            {{44, 45, 47}, .96f, 0.f, .55f},
                                            {{83, 119, 171}, .94f, 0.f, .55f},
                                            {{189, 25, 30}, .19f, 0.f, .22f},
                                            {{225, 229, 234}, .055f, 1.f, .006f},
                                            {{238, 249, 244}, .035f, 0.f, .01f},
                                            {{235, 227, 211}, .90f, 0.f, .35f},
                                            {{225, 226, 228}, .31f, 1.f, .04f},
                                            {{88, 86, 82}, .89f, 1.f, .85f},
                                            {{164, 159, 149}, .93f, 0.f, .75f},
                                            {{22, 76, 160}, .26f, 0.f, .40f},
                                            {{235, 230, 217}, .14f, 0.f, .10f}}};
float control(const Params &p, const char *key, float fallback, float lo = 0.f, float hi = 1.f) {
    return std::clamp(p.getFloat(key, fallback), lo, hi);
}
}  // namespace

eve::Result<std::unique_ptr<PbrTextureSet>> EverydayMaterials::generate(EverydayMaterial family, const Params &params) {
    using Output     = eve::Result<std::unique_ptr<PbrTextureSet>>;
    const auto index = static_cast<size_t>(family);
    if (index >= profiles.size() || params.getWidth() > 2048 || params.getHeight() > 2048)
        return Output::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "Everyday material requires a known family and dimensions at most 2048"));
    const auto  profile   = profiles[index];
    const float detail    = control(params, "detail", .5f);
    const float variation = control(params, "variation", .4f);
    const float finish    = control(params, "finish", .5f);
    // Material scale is independent of output resolution. Supersample unresolved maps
    // instead of changing the size or count of physical features.
    const int   repeats = int(std::round(control(params, "scale", 4.f, 1.f, 32.f)));
    const float du = 1.f / float(params.getWidth()), dv = 1.f / float(params.getHeight());
    const bool  prefilter = std::min(params.getWidth(), params.getHeight()) < repeats * 180;
    auto        sample    = [family, seed = params.getSeed(), repeats, du, dv, prefilter](float u, float v) {
        if (!prefilter) return detail::sampleEveryday(family, u, v, seed, repeats, 0);
        detail::EverydaySample sum{0.f, 0.f, 0.f};
        for (float y : {-.25f, .25f})
            for (float x : {-.25f, .25f}) {
                const auto s = detail::sampleEveryday(family, u + x * du, v + y * dv, seed, repeats, 0);
                sum.pigment += s.pigment * .25f;
                sum.relief += s.relief * .25f;
                sum.roughness += s.roughness * .25f;
            }
        return sum;
    };
    TextureRecipeDef def;
    def.id     = "everyday";
    def.height = [sample, profile, detail](float u, float v, const NoiseField &) {
        return .5f + (sample(u, v).relief - .5f) * profile.relief * (.25f + 1.5f * detail);
    };
    def.colorField = [sample, variation](float u, float v, const NoiseField &) {
        return .5f + (sample(u, v).pigment - .5f) * (variation * 2.f);
    };
    def.roughnessField = [sample](float u, float v, const NoiseField &) { return sample(u, v).roughness; };
    const Rgba8 tint{static_cast<uint8_t>(control(params, "tintR", profile.color.r / 255.f) * 255.f),
                     static_cast<uint8_t>(control(params, "tintG", profile.color.g / 255.f) * 255.f),
                     static_cast<uint8_t>(control(params, "tintB", profile.color.b / 255.f) * 255.f), 255};
    if (family == EverydayMaterial::Marble || family == EverydayMaterial::Wood) {
        auto mineral = [&](float t, float r, float g, float b) {
            def.albedo.add(t, uint8_t(std::clamp(r * tint.r, 0.f, 255.f)), uint8_t(std::clamp(g * tint.g, 0.f, 255.f)),
                           uint8_t(std::clamp(b * tint.b, 0.f, 255.f)));
        };
        if (family == EverydayMaterial::Wood) {
            mineral(0.f, .25f, .19f, .13f);
            mineral(.30f, .55f, .50f, .40f);
            mineral(.50f, 1.f, 1.f, 1.f);
            mineral(.70f, 1.25f, 1.32f, 1.45f);
            mineral(1.f, 1.35f, 1.55f, 1.90f);
        } else {
            mineral(0.f, .25f, .26f, .27f);
            mineral(.28f, .63f, .59f, .51f);
            mineral(.40f, .91f, .86f, .76f);
            mineral(.50f, 1.f, 1.f, 1.f);
            mineral(.75f, 1.03f, 1.04f, 1.07f);
            mineral(1.f, 1.08f, 1.10f, 1.14f);
        }
    } else {
        def.albedo.add(0.f, uint8_t(tint.r * .35f), uint8_t(tint.g * .35f), uint8_t(tint.b * .35f));
        def.albedo.add(.5f, tint.r, tint.g, tint.b);
        def.albedo.add(1.f, uint8_t(tint.r + (255 - tint.r) * .2f), uint8_t(tint.g + (255 - tint.g) * .2f),
                       uint8_t(tint.b + (255 - tint.b) * .2f));
    }
    const float roughness  = std::clamp(profile.roughness + (.5f - finish) * .26f, .025f, 1.f);
    def.pbr.roughnessLow   = std::max(.025f, roughness - .09f);
    def.pbr.roughnessHigh  = std::min(1.f, roughness + .09f);
    def.pbr.metallic       = profile.metallic;
    def.pbr.normalStrength = float(std::min(params.getWidth(), params.getHeight())) / 80.f;
    def.pbr.aoStrength     = family == EverydayMaterial::Glass ? 0.f : 1.f;
    Params effective;
    effective.setSize(params.getWidth(), params.getHeight());
    effective.setSeed(params.getSeed());
    effective.setFloat("scale", 1.f);
    effective.setInt("colors", 256);
    effective.setBool("seamless", true);
    std::string error;
    auto        maps = generatePbrSet(def, effective, error);
    if (!maps) return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, error));
    if (family == EverydayMaterial::Glass) {
        maps->transmission = .96f;
        maps->ior          = 1.5f;
    }
    return Output::success(std::move(maps));
}
}  // namespace eve::procgen
