#pragma once
#include "common/Export.h"

/**
 * @file AttackVfxRecipe.h
 * @brief Data-driven layered attack VFX recipe and element skin (Phase 1 schema).
 *
 * Attack presentation is composed of ordered phases and typed layer slots that
 * reference existing providers (MeshVFX, particles, decal, camera, …). Element
 * identity is a reusable AttackVfxSkin (palette / profiles / overlay URI), not a
 * hard-coded fire/water code path.
 *
 * Phase 1 owns schema validation and serialization only; layer executors arrive
 * in later phases. Runtime orchestration lives in AttackVfxRuntime.
 */

#include "common/Identity.h"
#include "common/Result.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::stylize {

/** @brief Attack presentation phase along the cast → hit → linger timeline. */
enum class AttackVfxPhaseKind : std::uint8_t {
    Anticipate, /**< Charge / windup. */
    Release,    /**< Strike / muzzle / whip spawn. */
    Travel,     /**< Projectile or sustained travel body. */
    Impact,     /**< Hit burst / cones / flash. */
    Aftermath,  /**< Smoke, debris, lingering embers. */
    Status      /**< Sustained target overlay (wet / burn / freeze). */
};

/** @brief Which existing presentation backend a layer binds to. */
enum class AttackVfxLayerRole : std::uint8_t {
    BlockingMesh, /**< Untextured / placeholder mesh timing. */
    MeshVfx,      /**< stylize::MeshVfxAsset URI. */
    Trail,        /**< Ribbon / weapon trail. */
    Particles,    /**< particles::ParticleEffect URI. */
    Decal,        /**< Surface projection. */
    Distortion,   /**< Local screen / scene-color warp. */
    Camera,       /**< Impulse / FOV shake profile. */
    Prefab,       /**< gameplay:prefab-spawn style mesh placeholder. */
    Audio         /**< presentation:audio* URI. */
};

/** @brief Exit policy when a phase or instance stops. */
enum class AttackVfxStopBehavior : std::uint8_t {
    StopEmitting,     /**< Stop new spawns; allow residual lifetime. */
    ClearImmediately  /**< Tear down owned presentation immediately. */
};

/** @brief How a layer follows its resolved spatial anchor. */
enum class AttackVfxSpatialAttachment : std::uint8_t {
    FollowTarget,
    FollowPositionOnly,
    WorldTransformAtStart
};

/** @brief Which request endpoint supplies the spatial anchor. */
enum class AttackVfxSpatialAnchor : std::uint8_t { Source, Target };

/** @brief Owning three-component float used at the recipe boundary. */
struct AttackVfxVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    auto operator<=>(const AttackVfxVec3&) const = default;
};

/**
 * @brief Backend-neutral spatial binding stored on a layer.
 *
 * Mirrors ActionSpatialBinding field semantics without depending on the action
 * module. Adapters resolve bones and entity handles at play/update time.
 */
struct EVENGINE_API_WORLD AttackVfxSpatial {
    AttackVfxSpatialAttachment attachment = AttackVfxSpatialAttachment::FollowTarget;
    AttackVfxSpatialAnchor     anchor     = AttackVfxSpatialAnchor::Source;
    std::size_t                targetIndex = 0;
    std::string                bone;
    AttackVfxVec3              positionOffset;
    AttackVfxVec3              rotationOffsetDegrees;
    AttackVfxVec3              scale{1.f, 1.f, 1.f};

    auto operator<=>(const AttackVfxSpatial&) const = default;

    /** @brief Validate finite offsets and strictly positive scale. */
    [[nodiscard]] Result<void> validate(std::string_view path) const;
};

/**
 * @brief One presentation layer slot inside a phase.
 *
 * @remarks Camera and Distortion may omit uri when floatParams carry the
 *          profile; every other role requires a non-empty uri.
 */
struct EVENGINE_API_WORLD AttackVfxLayer {
    AttackVfxLayerRole    role         = AttackVfxLayerRole::Particles;
    std::string           uri;
    AttackVfxSpatial      spatial;
    std::map<std::string, float> floatParams;
    AttackVfxStopBehavior stopBehavior = AttackVfxStopBehavior::StopEmitting;

    /** @brief Validate role/uri/spatial/parameter invariants. */
    [[nodiscard]] Result<void> validate(std::string_view path) const;
};

/**
 * @brief One timeline phase that owns zero or more layers.
 *
 * Empty startCue means the phase arms at play() after startOffsetSeconds.
 * Non-empty startCue waits for AttackVfxRuntime::signal(). durationSeconds <= 0
 * means the phase stays until endCue, stop(), or instance completion.
 */
struct EVENGINE_API_WORLD AttackVfxPhase {
    AttackVfxPhaseKind         kind               = AttackVfxPhaseKind::Release;
    std::string                startCue;
    std::string                endCue;
    double                     startOffsetSeconds = 0.0;
    double                     durationSeconds    = 0.0;
    std::vector<AttackVfxLayer> layers;

    /** @brief Validate cues, timing, and nested layers. */
    [[nodiscard]] Result<void> validate(std::string_view path) const;
};

/** @brief RGB palette used as a skin tint (components in [0, 1]). */
struct AttackVfxPalette {
    AttackVfxVec3 primary{1.f, 1.f, 1.f};
    AttackVfxVec3 secondary{1.f, 1.f, 1.f};
    AttackVfxVec3 emissive{1.f, 1.f, 1.f};

    auto operator<=>(const AttackVfxPalette&) const = default;
};

/**
 * @brief Reusable elemental / style skin applied at play time.
 *
 * Skins do not fork logic. They supply palette, style hint aliases, shake /
 * distortion profiles, and an optional sustained status overlay URI.
 */
struct EVENGINE_API_WORLD AttackVfxSkin {
    LogicalId                        id;
    AttackVfxPalette                 palette;
    std::map<std::string, std::string> styleHints;
    std::map<std::string, float>     shakeProfile;
    std::map<std::string, float>     distortionProfile;
    std::string                      statusOverlayUri;

    /** @brief Validate id, palette range, and finite profile values. */
    [[nodiscard]] Result<void> validate() const;

    /**
     * @brief Parse a standalone skin document.
     * @param json UTF-8 JSON with schema eve.stylize.attack-vfx-skin.
     */
    [[nodiscard]] static Result<AttackVfxSkin> fromJson(std::string_view json);

    /** @brief Serialize in canonical current-schema JSON. */
    [[nodiscard]] Result<std::string> toJson() const;
};

/** @brief Optional quality / budget knobs evaluated by later executor phases. */
struct AttackVfxBudget {
    int  lod              = 0;
    int  maxParticles     = 0; /**< 0 means unlimited. */
    bool allowDistortion  = true;

    auto operator<=>(const AttackVfxBudget&) const = default;
};

/**
 * @brief Versioned attack VFX composition recipe.
 *
 * @ownership Immutable input once validated; runtime copies into live state.
 * @thread Affine to the authoring / simulation owner; no internal locks.
 */
struct EVENGINE_API_WORLD AttackVfxRecipe {
    static constexpr std::string_view schemaId      = "eve.stylize.attack-vfx";
    static constexpr std::uint32_t    schemaVersion = 1;

    LogicalId                  id;
    std::vector<AttackVfxPhase> phases;
    std::optional<LogicalId>   skinId; /**< External skin reference. */
    std::optional<AttackVfxSkin> skin; /**< Optional inline skin for self-contained assets. */
    AttackVfxBudget            budget;

    /**
     * @brief Parse, validate, and return a complete recipe without mutating live state.
     * @param json UTF-8 JSON. Unknown fields are rejected.
     */
    [[nodiscard]] static Result<AttackVfxRecipe> fromJson(std::string_view json);

    /** @brief Serialize this recipe in canonical current-schema JSON form. */
    [[nodiscard]] Result<std::string> toJson() const;

    /** @brief Validate id, phases, optional skin, and budget invariants. */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief Transactional hot-reload boundary for one attack VFX recipe.
 * A failed reload preserves both the previous recipe and revision.
 */
class EVENGINE_API_WORLD AttackVfxRecipeSlot {
public:
    /** @brief Construct a slot from an already validated recipe. */
    explicit AttackVfxRecipeSlot(AttackVfxRecipe recipe);

    /** @brief Parse and atomically replace the recipe on success. */
    [[nodiscard]] Result<std::uint64_t> reload(std::string_view json);
    /** @brief Current immutable recipe snapshot. */
    [[nodiscard]] const AttackVfxRecipe& recipe() const noexcept { return recipe_; }
    /** @brief Monotonic successful-reload revision, starting at one. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    AttackVfxRecipe recipe_;
    std::uint64_t   revision_ = 1;
};

/** @brief Stable JSON spelling for a phase kind. */
[[nodiscard]] EVENGINE_API_WORLD std::string_view attackVfxPhaseKindName(AttackVfxPhaseKind kind) noexcept;
/** @brief Stable JSON spelling for a layer role. */
[[nodiscard]] EVENGINE_API_WORLD std::string_view attackVfxLayerRoleName(AttackVfxLayerRole role) noexcept;
/** @brief Stable JSON spelling for stop behavior. */
[[nodiscard]] EVENGINE_API_WORLD std::string_view attackVfxStopBehaviorName(AttackVfxStopBehavior behavior) noexcept;
/** @brief Stable JSON spelling for spatial attachment. */
[[nodiscard]] EVENGINE_API_WORLD std::string_view attackVfxSpatialAttachmentName(
    AttackVfxSpatialAttachment mode) noexcept;
/** @brief Stable JSON spelling for spatial anchor. */
[[nodiscard]] EVENGINE_API_WORLD std::string_view attackVfxSpatialAnchorName(AttackVfxSpatialAnchor anchor) noexcept;

}  // namespace eve::stylize
