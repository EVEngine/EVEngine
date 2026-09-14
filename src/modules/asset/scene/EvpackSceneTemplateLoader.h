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

/** @brief Immutable asset identities associated with a persisted scene node; no runtime objects are retained. */
struct SceneMeshBinding {
    SceneObjectId object;
    AssetRef      mesh;
    AssetRef      material;
    bool          enabled = true;
};

/** @brief Owning declarative scene tree and render bindings; neither mutates a Scene or retains GPU objects. */
struct LoadedSceneTemplate {
    AssetRef                      asset;
    scene::NodeDesc               root;
    asset::EvpackVariantSelection variant;
    std::vector<SceneMeshBinding> renderers;
};

/** @brief Capability-aware scene-template/2 decoder with explicit N-1 hierarchy-only compatibility. */
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
