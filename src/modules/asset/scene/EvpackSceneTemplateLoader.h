#pragma once

/** @file EvpackSceneTemplateLoader.h @brief Runtime decoding of canonical scene templates. */

#include "asset/EvpackResourceReader.h"
#include "scene/NodeDesc.h"

namespace eve::asset_scene {

/** @brief Bounds for untrusted scene-template metadata. */
struct SceneTemplateLoadLimits {
    std::uint32_t maximumNodes = 1'000'000;
    std::uint32_t maximumDepth = 4096;
    std::uint64_t maximumDecodedBytes = 256ull * 1024ull * 1024ull;
};

/** @brief Owning declarative scene tree ready for `Scene::mount`. */
struct LoadedSceneTemplate {
    AssetRef                      asset;
    scene::NodeDesc               root;
    asset::EvpackVariantSelection variant;
};

/** @brief Capability-aware `eve.scene-template/1` to Scene::NodeDesc adapter. */
class EvpackSceneTemplateLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackSceneTemplateLoader(const asset::EvpackResourceReader& reader) noexcept
        : reader_(reader) {}

    /**
     * @brief Validate identities, hierarchy and finite TRS values before building a tree.
     * @return Owning NodeDesc candidate; the Scene is not mutated by this operation.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedSceneTemplate> load(
        const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities,
        const SceneTemplateLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_scene
