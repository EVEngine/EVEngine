#pragma once

#include "common/Result.h"
#include "common/AttachmentPoint.h"
#include "common/AnimationEventSource.h"
#include "stylize/MeshEffect.h"
#include "stylize/TrailEffect.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::stylize {

/** @brief One normalized key in a deterministic float parameter curve. */
struct MeshVfxFloatKey {
    float time = 0.f;
    float value = 0.f;
};

/** @brief Piecewise-linear parameter animation evaluated over one playback cycle. */
struct MeshVfxFloatCurve {
    std::vector<MeshVfxFloatKey> keys;

    /** @brief Evaluate the curve at normalized time, clamped to its first and last keys. */
    [[nodiscard]] float evaluate(float normalizedTime) const noexcept;
};

/** @brief Named gameplay/render marker on the primary layer's normalized playback timeline. */
struct MeshVfxEventAsset {
    float       time = 0.f;
    std::string name;
};

/** @brief Authored root/tip attachment pair used to sample a weapon ribbon automatically. */
struct MeshVfxTrailBinding {
    std::string          rootAttachment;
    std::string          tipAttachment;
    eve::AttachmentPoint rootOffset;
    eve::AttachmentPoint tipOffset;
};

/** @brief Animation notification names mapped to built-in Mesh VFX lifecycle actions. */
struct MeshVfxAnimationTriggers {
    std::vector<std::string> play;
    std::vector<std::string> stop;
    std::vector<std::string> trailBreak;
};

/** @brief Summary of lifecycle actions dispatched from one animation event batch. */
struct MeshVfxAnimationDispatch {
    std::size_t handled = 0;
    std::size_t played = 0;
    std::size_t stopped = 0;
    std::size_t trailBreaks = 0;
};

/** @brief One independently rendered layer in a mesh VFX asset. */
struct MeshVfxLayerAsset {
    std::string                  style;
    MeshEffectPlayback          playback;
    std::map<std::string, float> floatParameters;
    std::map<std::string, MeshVfxFloatCurve> floatCurves;
};

/** @brief Versioned, backend-neutral description of a real-time mesh effect. */
struct MeshVfxAsset {
    static constexpr std::string_view schemaId = "eve.stylize.mesh-vfx";
    static constexpr std::uint32_t schemaVersion = 1;

    std::vector<MeshVfxLayerAsset> layers;
    std::optional<TrailSettings>   trail;
    std::optional<MeshVfxTrailBinding> trailBinding;
    std::vector<MeshVfxEventAsset> events;
    MeshVfxAnimationTriggers       animationTriggers;

    /**
     * @brief Parse, migrate, and validate a JSON asset without mutating live state.
     * @param json UTF-8 JSON text. Version 0 single-style assets are migrated to version 1.
     * @return Complete asset or a structured diagnostic. Unknown fields are rejected.
     */
    [[nodiscard]] static eve::Result<MeshVfxAsset> fromJson(std::string_view json);

    /**
     * @brief Serialize this asset in canonical current-schema JSON form.
     * @return UTF-8 JSON or a structured serialization diagnostic.
     */
    [[nodiscard]] eve::Result<std::string> toJson() const;

    /** @brief Validate all layer, playback, parameter, and trail invariants. */
    [[nodiscard]] eve::Result<void> validate() const;
};

/**
 * @brief Owning runtime instantiated from one immutable MeshVfxAsset snapshot.
 * @ownership Owns all layer runtimes and the optional trail; target meshes remain borrowed by renderer resolution.
 * @thread Simulation-thread affine. Rendering must consume layers on the render thread.
 * @reentrancy Does not invoke callbacks.
 */
class MeshVfxAssetInstance {
public:
    /**
     * @brief Build independent runtime state for every asset layer.
     * @return Owned instance or a diagnostic when a style or parameter is incompatible.
     */
    [[nodiscard]] static eve::Result<std::unique_ptr<MeshVfxAssetInstance>> create(const MeshVfxAsset& asset);

    /** @brief Bind every layer to the same authoritative target handle. */
    void bindTarget(MeshEffectTargetHandle target);
    /** @brief Begin every layer and clear prior trail samples. */
    void play() noexcept;
    /** @brief Stop every layer using an optional fade override. */
    void stop(float fadeOutSeconds = -1.f);
    /** @brief Advance every layer and the optional trail with caller-supplied simulation time. */
    void update(float dtSeconds);
    /**
     * @brief Move all timeline events crossed since the previous drain.
     * @return Ordered event names. No user callback is invoked by update().
     */
    [[nodiscard]] std::vector<std::string> drainEvents();
    /** @brief Sample authored root/tip attachments and append one trail segment. */
    [[nodiscard]] eve::Result<TrailAppendResult> sampleTrail(const eve::IAttachmentPointSource& source);
    /** @brief Apply authored lifecycle mappings for events emitted by the latest animation update. */
    [[nodiscard]] MeshVfxAnimationDispatch processAnimationEvents(const eve::IAnimationEventSource& source);

    /** @brief Return the number of independently rendered mesh layers. */
    [[nodiscard]] std::size_t layerCount() const noexcept { return layers_.size(); }
    /** @brief Return one owned layer; throws std::out_of_range for an invalid index. */
    [[nodiscard]] MeshEffectInstance& layer(std::size_t index) { return *layers_.at(index); }
    /** @brief Return one owned layer; throws std::out_of_range for an invalid index. */
    [[nodiscard]] const MeshEffectInstance& layer(std::size_t index) const { return *layers_.at(index); }
    /** @brief Whether this composition owns a trail emitter. */
    [[nodiscard]] bool hasTrail() const noexcept { return trail_ != nullptr; }
    /** @brief Return the owned trail; throws when this asset has no trail. */
    [[nodiscard]] TrailEmitter& trail();

private:
    explicit MeshVfxAssetInstance(const MeshVfxAsset& asset);

    std::vector<std::unique_ptr<MeshEffectInstance>> layers_;
    std::vector<std::map<std::string, MeshVfxFloatCurve>> curves_;
    std::unique_ptr<TrailEmitter>                    trail_;
    std::optional<MeshVfxTrailBinding>               trailBinding_;
    std::vector<MeshVfxEventAsset>                   events_;
    MeshVfxAnimationTriggers                         animationTriggers_;
    std::vector<std::string>                         pendingEvents_;
    float                                            previousNormalizedTime_ = 0.f;
};

/**
 * @brief Transactional hot-reload boundary for one mesh VFX asset.
 * A failed reload preserves both the previous asset and revision. Filesystem watching belongs to the caller.
 * @thread Affine to the owning asset/editor thread; no internal synchronization.
 */
class MeshVfxAssetSlot {
public:
    /** @brief Construct a slot from an already validated asset. */
    explicit MeshVfxAssetSlot(MeshVfxAsset asset);

    /** @brief Parse and atomically replace the asset on success. */
    [[nodiscard]] eve::Result<std::uint64_t> reload(std::string_view json);
    /** @brief Current immutable asset snapshot. */
    [[nodiscard]] const MeshVfxAsset& asset() const noexcept { return asset_; }
    /** @brief Monotonic successful-reload revision, starting at one. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    MeshVfxAsset asset_;
    std::uint64_t revision_ = 1;
};

}  // namespace eve::stylize
