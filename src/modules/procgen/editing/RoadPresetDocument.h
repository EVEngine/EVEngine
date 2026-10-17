#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "common/Export.h"
#include "common/Value.h"
#include "procgen/road/RoadNetwork.h"
namespace eve::procgen_editing {
/** @brief Field-level author overrides; absent fields inherit, present arrays replace as a whole. */
struct RoadStyleOverrides {
    std::optional<procgen::road::RoadTerrainPolicy> terrainPolicy;  ///< Whole inherited terrain policy.
    std::optional<procgen::road::RoadSidePlacement> sidePlacement;  ///< Whole inherited rigid-placement rule.
    std::optional<procgen::road::RoadSideTerrainRule>
        sideTerrain;  ///< Whole inherited footprint and height-selection rule.
    std::optional<procgen::road::RoadSamplingPolicy> samplingPolicy;   ///< Explicit inherited metric-selection policy.
    std::optional<float> samplingSpacingMeters, samplingAngleDegrees;  ///< Inherited metric sampling settings.
    std::optional<int>   crossSectionSubdivisions;                     ///< Inherited bounded section subdivision count.
    std::optional<procgen::road::RoadRollInterpolation> rollInterpolation;  ///< Explicit roll policy; absence inherits.
    std::optional<procgen::road::RoadFrameOrientation>
        frameOrientation;  ///< Explicit frameOrientation override; absence retains the inherited value.
    std::optional<procgen::road::RoadWidthInterpolation>
        widthInterpolation;  ///< Explicit widthInterpolation override; absence retains the inherited value.
    std::optional<std::vector<procgen::road::RoadProfilePoint>>
        crossSection;  ///< Explicit crossSection override; absence retains the inherited value.
    std::optional<bool>
        crossSectionClosed;  ///< Explicit crossSectionClosed override; absence retains the inherited value.
    std::optional<bool>
        automaticStructures;  ///< Explicit automaticStructures override; absence retains the inherited value.
    std::optional<std::vector<procgen::road::RoadStructureInterval>>
        structures;  ///< Explicit structures override; absence retains the inherited value.
    std::optional<std::vector<procgen::road::RoadContinuousFacility>>
                         facilities;      ///< Explicit facilities override; absence retains the inherited value.
    std::optional<float> laneWidth;       ///< Explicit laneWidth override; absence retains the inherited value.
    std::optional<float> curbWidth;       ///< Explicit curbWidth override; absence retains the inherited value.
    std::optional<float> curbHeight;      ///< Explicit curbHeight override; absence retains the inherited value.
    std::optional<float> sidewalkWidth;   ///< Explicit sidewalkWidth override; absence retains the inherited value.
    std::optional<float> sidewalkHeight;  ///< Explicit sidewalkHeight override; absence retains the inherited value.
    std::optional<float> deckThickness;   ///< Explicit deckThickness override; absence retains the inherited value.
    std::optional<float> pierWidth;       ///< Explicit pierWidth override; absence retains the inherited value.
    std::optional<float> pierDepth;       ///< Explicit pierDepth override; absence retains the inherited value.
    std::optional<float> pierSpacing;     ///< Explicit pierSpacing override; absence retains the inherited value.
    std::optional<float> pierClearance;   ///< Explicit pierClearance override; absence retains the inherited value.
    std::optional<float> markingWidth;    ///< Explicit markingWidth override; absence retains the inherited value.
    std::optional<float> dashLength;      ///< Explicit dashLength override; absence retains the inherited value.
    std::optional<float> dashGap;         ///< Explicit dashGap override; absence retains the inherited value.
    std::optional<float> uvMeters;        ///< Explicit uvMeters override; absence retains the inherited value.
    std::optional<float> speedLimitMps;   ///< Explicit speedLimitMps override; absence retains the inherited value.
    std::optional<float>
        sideObjectStartOffset;  ///< Explicit sideObjectStartOffset override; absence retains the inherited value.
    std::optional<float>
        sideObjectEndOffset;  ///< Explicit sideObjectEndOffset override; absence retains the inherited value.
    std::optional<int>  trafficPriority;  ///< Explicit trafficPriority override; absence retains the inherited value.
    std::optional<bool> sideObjectsLeft;  ///< Explicit sideObjectsLeft override; absence retains the inherited value.
    std::optional<procgen::road::RoadSideDistribution>
                        sideObjectDistribution;  ///< Explicit rigid-anchor distribution override.
    std::optional<bool> sideObjectsRight;  ///< Explicit sideObjectsRight override; absence retains the inherited value.
};
/** @brief Optional marker defaults; presence overrides the inherited value or unbound source profile. */
struct RoadPointOverrides {
    std::optional<float>                          widthScale, rollDegrees;
    std::optional<float>                          rollCenterOffset;  ///< Inherited transverse roll axis in [-.5,.5].
    std::optional<float>                          splineStrength;    ///< Optional outgoing-span tangent strength.
    std::optional<procgen::road::RoadPathControl> pathControl;       ///< Explicit outgoing span controller override.
    std::optional<bool> alignStraightStart, alignStraightEnd;        ///< Optional horizontal join constraints.
    std::optional<bool> circularFromEnd;  ///< Optional endpoint reference for a circular span.
};
/** @brief Explicit marker profile override, applied after preset and road defaults. */
struct RoadMarkerOverride {
    std::uint32_t      markerIndex = 0;
    RoadPointOverrides overrides;
};
/** @brief Per-facility instance patch; identity must exist in the resolved style. */
struct RoadFacilityOverrides {
    std::uint32_t                                               id = 0;
    std::optional<float>                                        start, end, lateralOffset, verticalOffset, uvMeters;
    std::optional<bool>                                         capStart, capEnd;
    std::optional<std::vector<procgen::road::RoadProfilePoint>> profile;
    std::optional<procgen::road::RoadFacilityPrefab>            prefab;
};
/** @brief Author-only edge metadata; locks reject direct graph/binding edits, not inherited updates. */
struct RoadAuthorMetadata {
    std::uint32_t edgeId = 0;
    std::string   name;
    bool          locked = false;
};
/** @brief Stable document-local shared preset; parents form a bounded acyclic definition graph. */
struct RoadTypePreset {
    std::string        id, parentId;  ///< Owning UTF-8 identity and optional inherited preset identity.
    RoadStyleOverrides overrides;     ///< Root inherits native RoadStyle defaults before its explicit fields.
    RoadPointOverrides pointDefaults;
};
/** @brief One edge binding; marker overrides follow inherited and road profile defaults. */
struct RoadTypeBinding {
    std::uint32_t                      edgeId = 0;  ///< Stable edge identity in this document's source graph.
    std::string                        typeId;  ///< Shared preset identity; dangling references reject the candidate.
    RoadStyleOverrides                 overrides;  ///< Instance fields override the complete parent chain.
    RoadPointOverrides                 pointDefaults;
    std::vector<RoadMarkerOverride>    markers;
    std::vector<RoadFacilityOverrides> facilities;
};
/** @brief Owning edit builder; edges without preset bindings retain their authored source styles. */
struct RoadPresetDraft {
    procgen::road::RoadNetwork      source;
    std::vector<RoadTypePreset>     types;
    std::vector<RoadTypeBinding>    bindings;
    std::vector<RoadAuthorMetadata> metadata;
};
class RoadPresetDocument;
/** @brief Opaque prepared state bound to one live document instance and revision. */
class EVENGINE_API_ORCHESTRATION PreparedRoadPresetEdit {
public:
    /** @brief Release owning candidate state; no automatic publication. */
    ~PreparedRoadPresetEdit();
    /** @brief Transfer a prepared candidate without committing it. */
    PreparedRoadPresetEdit(PreparedRoadPresetEdit&&) noexcept;
    /** @brief Transfer a prepared candidate without committing it. */
    PreparedRoadPresetEdit& operator=(PreparedRoadPresetEdit&&) noexcept;
    PreparedRoadPresetEdit(const PreparedRoadPresetEdit&)            = delete;
    PreparedRoadPresetEdit& operator=(const PreparedRoadPresetEdit&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit PreparedRoadPresetEdit(std::unique_ptr<Impl> impl);
    friend class RoadPresetDocument;
};
/** @brief Single owner of source author graph, shared definitions, bindings and bounded undo history.
 * Synchronous owner-thread only, callback-free; all inputs are owning copies. Resolved networks are derived values.
 */
class EVENGINE_API_ORCHESTRATION RoadPresetDocument {
public:
    /** @brief Build a document after validating its complete author state.
     * @return Owning document or diagnostic; no partial state escapes.
     * @cost Graph-sized validation and definition resolution, amortized at author document creation.
     */
    [[nodiscard]] static eve::Result<std::unique_ptr<RoadPresetDocument>> create(RoadPresetDraft draft);
    /** @brief Release document and all author history; outstanding tokens cannot target another document. */
    ~RoadPresetDocument();
    RoadPresetDocument(const RoadPresetDocument&)            = delete;
    RoadPresetDocument& operator=(const RoadPresetDocument&) = delete;
    /** @brief Begin an owning edit builder; modifying it cannot change the live document.
     * @cost Linear copy of current author data, amortized per edit transaction.
     */
    [[nodiscard]] RoadPresetDraft beginEdit() const;
    /** @brief Validate and resolve an owning builder without publishing it.
     * @return Instance/revision-bound opaque commit token or diagnostic.
     * @cost Definition traversal and graph copies plus bound-edge validation, amortized per author transaction.
     */
    [[nodiscard]] eve::Result<PreparedRoadPresetEdit> prepare(RoadPresetDraft draft) const;
    /** @brief Publish a validated token if its owner/revision still matches; reject stale, foreign or consumed tokens.
     * @return Success or diagnostic; failure preserves state and redo history.
     * @cost At most 128 shared history references; no graph resolution or baking at commit.
     */
    [[nodiscard]] eve::Result<void> commit(PreparedRoadPresetEdit edit);
    /** @brief Return an immutable resolved graph for direct bakeRoadNetwork consumption.
     * @ownership Borrowed from this document; valid until commit, undo, redo or destruction.
     * @thread Owner-thread synchronous scope only; do not retain across edits.
     */
    [[nodiscard]] const procgen::road::RoadNetwork& network() const;
    /** @brief Restore the prior complete author/derived state as a new document revision. */
    [[nodiscard]] eve::Result<void> undo();
    /** @brief Restore the next complete author/derived state as a new document revision. */
    [[nodiscard]] eve::Result<void> redo();
    /** @brief Capture canonical author history and cursor; derived networks and runtime tokens are not serialized.
     * @cost Linear in retained author history; bounded to 8 MiB of JSON, amortized per save boundary.
     */
    [[nodiscard]] eve::Result<eve::Value> capture() const;
    /** @brief Decode, migrate and resolve every history frame privately before issuing a commit token.
     * @return Prepared restore or diagnostic; unknown object fields ignored, unknown schema/version rejected.
     * @cost Linear history decoding plus graph resolution, amortized per explicit load boundary.
     */
    [[nodiscard]] eve::Result<PreparedRoadPresetEdit> prepareRestore(const eve::Value& value) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit RoadPresetDocument(std::unique_ptr<Impl> impl);
};
}  // namespace eve::procgen_editing
