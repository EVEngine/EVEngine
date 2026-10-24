#pragma once

/** @file ActionPreviewProfile.h @brief Versioned action-editor preview settings and skeleton diagnostics. */

#include "action/ActionTimeline.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"
#include "editor/EditorIds.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eve::editor {

inline constexpr std::string_view kActionPreviewProfileSchemaId = "eve.action.preview-profile";
inline constexpr std::uint64_t    kActionPreviewProfileSchemaVersion = 1;

/** @brief Renderer-neutral shading preference persisted only for editor preview. */
enum class ActionPreviewShading : std::uint8_t { Lit, Unlit };

/** @brief Asset-specific editor state kept outside the runtime action timeline. */
struct ActionPreviewProfile {
    AssetGuid            actionAsset;
    std::string          previewModelUri;
    std::string          baseMotionUri;
    std::string          animationProfileUri;
    std::string          boneMaskUri;
    bool                 showRootMotion = true;
    bool                 footIk         = false;
    bool                 showGrid       = true;
    bool                 showSkeleton   = false;
    bool                 showJointNames = false;
    ActionPreviewShading shading        = ActionPreviewShading::Lit;

    /** @brief Validate stable identity, URI spelling and enum state. */
    [[nodiscard]] EVENGINE_API_EDITORS Result<void> validate() const;
    /** @brief Encode the canonical sidecar value. */
    [[nodiscard]] EVENGINE_API_EDITORS Result<Value> toValue() const;
    /** @brief Decode and validate a sidecar transactionally. */
    [[nodiscard]] static EVENGINE_API_EDITORS Result<ActionPreviewProfile> fromValue(const Value& value);

    auto operator<=>(const ActionPreviewProfile&) const = default;
};

/** @brief One owning bone entry used across optional asset-provider boundaries. */
struct ActionPreviewBone {
    std::string name;
    int         parent = -1;

    auto operator<=>(const ActionPreviewBone&) const = default;
};

/** @brief Owning skeleton topology independent of animation backend object lifetimes. */
struct ActionPreviewSkeleton {
    std::string                    sourceUri;
    std::vector<ActionPreviewBone> bones;
};

/** @brief Optional provider implemented by an animation/model authoring integration. */
class IActionPreviewAssetProvider {
public:
    static constexpr const char* capabilityName = "eve.action.preview-asset-provider";
    virtual ~IActionPreviewAssetProvider() = default;

    /** @brief Resolve a model URI to an owning skeleton topology. */
    [[nodiscard]] virtual Result<ActionPreviewSkeleton> modelSkeleton(std::string_view modelUri) = 0;
    /** @brief Resolve an animation URI to an owning skeleton topology. */
    [[nodiscard]] virtual Result<ActionPreviewSkeleton> animationSkeleton(std::string_view animationUri) = 0;
    /** @brief Validate a mask against the target skeleton without retaining either argument. */
    [[nodiscard]] virtual Result<void> validateBoneMask(std::string_view boneMaskUri,
                                                       const ActionPreviewSkeleton& target) = 0;
};

/** @brief Severity of one animation section's compatibility with the preview target. */
enum class ActionPreviewCompatibility : std::uint8_t { Compatible, Retargetable, Incompatible, Unsupported };

/** @brief Owning diagnostic projection for one animation section. */
struct ActionPreviewSectionDiagnostic {
    LogicalId                  sectionId;
    ActionPreviewCompatibility compatibility = ActionPreviewCompatibility::Unsupported;
    std::vector<Diagnostic>    diagnostics;
};

/** @brief Complete compatibility report suitable for timeline decoration and tooltips. */
struct ActionPreviewCompatibilityReport {
    std::vector<ActionPreviewSectionDiagnostic> sections;
    std::vector<Diagnostic>                     profileDiagnostics;

    /** @brief True when no section or profile diagnostic prevents preview. */
    [[nodiscard]] EVENGINE_API_EDITORS bool canPreview() const noexcept;
};

/** @brief Stateless deterministic compatibility analysis over owning provider snapshots. */
class ActionPreviewCompatibilityAnalyzer {
public:
    /**
     * @brief Analyze every animation section against the selected model or first animation anchor.
     * @param provider Optional authoring provider; absence is reported as Unsupported, never silently accepted.
     */
    [[nodiscard]] EVENGINE_API_EDITORS Result<ActionPreviewCompatibilityReport> analyze(
        const action::ActionTimeline& timeline, const ActionPreviewProfile& profile,
        IActionPreviewAssetProvider* provider) const;
};

/** @brief Disk sidecar store keyed only by stable action AssetGuid. */
class ActionPreviewProfileStore {
public:
    /** @brief Construct a store rooted at one project directory retained by value. */
    explicit EVENGINE_API_EDITORS ActionPreviewProfileStore(std::filesystem::path projectRoot);

    /** @brief Atomically validate and save one profile under `.eve/action-preview`. */
    [[nodiscard]] EVENGINE_API_EDITORS Result<void> save(const ActionPreviewProfile& profile) const;
    /** @brief Load and validate one profile without modifying caller state on failure. */
    [[nodiscard]] EVENGINE_API_EDITORS Result<ActionPreviewProfile> load(const AssetGuid& actionAsset) const;
    /** @brief Return the stable sidecar path for diagnostics and file watching. */
    [[nodiscard]] EVENGINE_API_EDITORS std::filesystem::path pathFor(const AssetGuid& actionAsset) const;

private:
    std::filesystem::path projectRoot_;
};

/** @brief Stable protocol spelling for a shading option. */
[[nodiscard]] EVENGINE_API_EDITORS std::string_view actionPreviewShadingName(ActionPreviewShading shading) noexcept;

}  // namespace eve::editor
