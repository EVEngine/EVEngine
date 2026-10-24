#pragma once
#include "common/Result.h"
#include "procgen/texture/PbrMaterial.h"

namespace eve::procgen {
/** @brief Everyday surface families; no scene, GPU or registry state is retained. */
enum class EverydayMaterial {
    Wood,
    Metal,
    Rubber,
    Cloth,
    Plastic,
    Steel,
    Glass,
    Paper,
    Aluminum,
    CastIron,
    Cement,
    Paint,
    Marble
};

/**
 * @brief CPU material factory with bounded, intuitive parameters.
 * @ownership Returned sets uniquely own their maps. Inputs are borrowed only for the call.
 * @thread Any thread with distinct inputs; no callbacks, registry access or retained pointers.
 * @remarks Same seed and effective parameters give identical bytes on one toolchain;
 * floating-point noise may differ across platforms. No simulation time or global RNG.
 */
class EVENGINE_API_DOMAINS EverydayMaterials {
public:
    /**
     * @brief Generate six aligned maps and optical surface metadata.
     * @param family Surface family.
     * @param params Size/seed and optional scale, detail, finish, variation, tintR/G/B.
     * Finite controls are clamped; invalid typed/nonfinite values use defaults.
     * @return Owned set or InvalidArgument for unsupported family/size above 2048.
     * @cost Expensive: linear in pixel count plus shared cavity sampling; generate once and reuse.
     */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> generate(EverydayMaterial family,
                                                                              const Params    &params);
    /** @brief Generate wood using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> wood(const Params &params) {
        return generate(EverydayMaterial::Wood, params);
    }
    /** @brief Generate metal using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> metal(const Params &params) {
        return generate(EverydayMaterial::Metal, params);
    }
    /** @brief Generate rubber using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> rubber(const Params &params) {
        return generate(EverydayMaterial::Rubber, params);
    }
    /** @brief Generate cloth using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> cloth(const Params &params) {
        return generate(EverydayMaterial::Cloth, params);
    }
    /** @brief Generate plastic using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> plastic(const Params &params) {
        return generate(EverydayMaterial::Plastic, params);
    }
    /** @brief Generate steel using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> steel(const Params &params) {
        return generate(EverydayMaterial::Steel, params);
    }
    /** @brief Generate glass using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> glass(const Params &params) {
        return generate(EverydayMaterial::Glass, params);
    }
    /** @brief Generate paper using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> paper(const Params &params) {
        return generate(EverydayMaterial::Paper, params);
    }
    /** @brief Generate aluminum using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> aluminum(const Params &params) {
        return generate(EverydayMaterial::Aluminum, params);
    }
    /** @brief Generate castiron using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> castIron(const Params &params) {
        return generate(EverydayMaterial::CastIron, params);
    }
    /** @brief Generate cement using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> cement(const Params &params) {
        return generate(EverydayMaterial::Cement, params);
    }
    /** @brief Generate paint using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> paint(const Params &params) {
        return generate(EverydayMaterial::Paint, params);
    }
    /** @brief Generate marble using the shared pipeline. @param params See generate().
     * @return Owned maps or structured diagnostic. @cost Expensive per pixel; cache the result. */
    [[nodiscard]] static eve::Result<std::unique_ptr<PbrTextureSet>> marble(const Params &params) {
        return generate(EverydayMaterial::Marble, params);
    }
};
}  // namespace eve::procgen
