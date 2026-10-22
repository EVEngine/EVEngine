#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string_view>
#include <vector>

namespace eve::procgen::road {

/** @brief Visual bridge topology presets; geometry generation performs no structural load analysis. */
enum class BridgeKind {
    Beam,
    Slab,
    Arch,
    TiedArch,
    ThroughTruss,
    DeckTruss,
    Suspension,
    CableStayed,
    Cantilever,
    Trestle
};
/** @brief Section construction shared by all bridge topologies. */
enum class BridgeSection { Box, Round, IBeam, TaperedBox };
/** @brief Orthogonal surface classification carried by each generated member. */
enum class BridgeMaterial {
    Concrete,
    Limestone,
    PaintedSteel,
    GalvanizedSteel,
    Timber,
    Asphalt,
    WhitePaint,
    YellowPaint
};
/** @brief Editable edge protection rule; presets select the reference's construction. */
enum class BridgeRailing { Metal, Concrete, Masonry, Timber };
/** @brief Semantic mesh groups, independent of graphics materials. */
enum class BridgePart {
    Deck,
    Surface,
    Pier,
    Abutment,
    Girder,
    Arch,
    Truss,
    Tower,
    Cable,
    Hanger,
    Railing,
    Marking,
    Footing,
    Cap,
    Sidewalk,
    Connection,
    Anchor
};
/** @brief Owned world-space position in metres, Y-up. */
struct BridgePoint {
    float x = 0.f, y = 0.f, z = 0.f;
};
/** @brief Owned bounded authoring parameters; all dimensions are metres, counts are deterministic. */
struct BridgeParameters {
    BridgeKind    kind = BridgeKind::Beam;
    BridgePoint   start{-24.f, 12.f, 0.f}, end{24.f, 12.f, 0.f};
    float         width = 7.f, sidewalkWidth = 1.f, deckThickness = .75f, groundY = 0.f;
    float         memberWidth = .4f, pierWidth = 1.3f, pierDepth = 1.3f;
    float         archRise = 6.f, towerHeight = 14.f, trussHeight = 5.f;
    float         cableRadius = .09f, cableSag = .65f, towerInset = .25f, towerTopWidthRatio = .15f;
    float         railingHeight = 1.1f, railingWidth = .12f;
    float         memberDepth = .55f, girderHeight = 1.1f, archRibWidth = .85f, archRibDepth = 1.0f;
    float         foundationHeight = .8f, foundationScale = 2.2f, pierCapHeight = .7f;
    float         abutmentDepth = 2.6f, wingWallLength = 4.f, uvMeters = 2.f;
    float         portalInset = .06f, cantileverCenterRatio = .28f, towerLegWidth = 1.6f;
    BridgeRailing railing = BridgeRailing::Metal;
    int           spans = 3, panels = 12, curveSegments = 32, cableSides = 8;
    int           railingBars = 4, plankCount = 120;
    bool          piers = true, railings = true, markings = true, crossBracing = true, connectionDetails = true;
};
/** @brief One owned editable structural member; width/height are transverse to its longitudinal axis.
 * Round sections use width as diameter. No external object references are retained.
 */
struct BridgeMember {
    BridgePoint    from, to;
    float          width = .4f, height = .4f;
    BridgeSection  section = BridgeSection::Box;
    BridgePart     part    = BridgePart::Girder;
    BridgePoint    widthAxis{};  ///< Projected transverse width direction; zero selects a world-up-derived frame.
    BridgeMaterial material = BridgeMaterial::Concrete;
    float endWidth = 0.f, endHeight = 0.f;  ///< Positive far-end dimensions for TaperedBox; zero for other sections.
    float uvMeters = 2.f;                   ///< Metres per repeat, independent of tessellation.
};
/** @brief Transient owning member plan; edit members to customize logic before baking. Not a persistent format. */
struct BridgePlan {
    std::vector<BridgeMember> members;
    int                       cableSides = 8;
};
/** @brief Static protocol name, or an empty view for an unknown enum; process-lifetime storage, thread-safe. */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view bridgeKindName(BridgeKind kind) noexcept;
/** @brief Static semantic name, or an empty view for an unknown enum; process-lifetime storage, thread-safe. */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view bridgePartName(BridgePart part) noexcept;
/** @brief Static surface name, or an empty view for an unknown enum; process-lifetime storage, thread-safe. */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view bridgeMaterialName(BridgeMaterial material) noexcept;
/** @brief Return an owning reference-aligned preset; unsupported kinds fail without side effects.
 * @param kind One of the ten visual topology families.
 * @return Independent parameters; synchronous, reentrant, no callbacks or external services.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<BridgeParameters> bridgePreset(BridgeKind kind);
/** @brief Plan the selected topology without renderer or terrain providers.
 * @param parameters Borrowed only during this synchronous reentrant call; never retained.
 * @return Independent editable plan or InvalidArgument; failure changes no observable state.
 * @cost Linear in spans times panels plus curve tessellation; prepare once per authoring edit, then bake.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<BridgePlan> planBridge(const BridgeParameters& parameters);
/** @brief Bake a validated member plan into independent CPU geometry; upload separately.
 * @param plan Borrowed during this synchronous reentrant call; unknown enum values and degenerate members fail.
 * @return Owning grouped mesh or diagnostic, with no partial publication. No callbacks/services are used.
 * @cost Linear in member count times section tessellation; amortize by caching until the plan changes.
 * @note Maximum 16384 members and 4 million vertex/index elements. Same plan yields deterministic topology;
 * floating-point positions are compared with relative tolerance 1e-5 across platforms.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshBuild> bakeBridge(const BridgePlan& plan);
/** @brief Decode recipe parameters into an owning typed authoring value.
 * @param params Borrowed for this call; bridgeType selects one of the ten named presets.
 * @return Validated authoring or a diagnostic. Synchronous and reentrant; no external state is changed.
 * @cost Constant bounded parameter decoding; plan and bake separately.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<BridgeParameters> bridgeParametersFromParams(const Params& params);
/** @brief Atomically fill absent recipe parameters from the selected bridge preset.
 * @param params Owner-thread mutable authoring data; explicit overrides are validated and preserved.
 * @return Success or a diagnostic without partial mutation; synchronous and reentrant on independent Params.
 * @cost Bounded parameter-map copying and insertion; apply once per edit, before planning.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> applyBridgeRecipeDefaults(Params& params);

}  // namespace eve::procgen::road
