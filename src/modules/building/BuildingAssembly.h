#pragma once
#include "common/Export.h"

#include "building/BuildingSnapshot.h"
#include "common/Identity.h"
#include "common/RuntimeHandle.h"

#include <memory>
#include <span>

namespace eve::building {
class PlacementWorld;
struct PreparedAssemblyData;

/** @brief Persistent identity of one placed house; distinct from a blueprint asset. */
struct AssemblyIdTag {};
using AssemblyId = eve::detail::Id128<AssemblyIdTag>;
/** @brief Stable blueprint-local part key, preserved through capture and instantiation. */
struct BlueprintPartId {
    uint32_t value                                    = 0;
    bool     operator==(const BlueprintPartId&) const = default;
};
/** @brief Owner-scoped, generation-checked runtime identity of a house. */
struct AssemblyHandleTag {};
struct AssemblyHandle {
    eve::RuntimeHandle<AssemblyHandleTag> slot;
    uint64_t                              ownerEpoch = 0;
};
/** @brief Integer grid-vertex origin, floor offset and quarter turns: (x,y) maps to (y,-x) for one turn. */
struct AssemblyPose {
    int   x            = 0;
    int   y            = 0;
    int   level        = 0;
    int   quarterTurns = 0;
    float elevation    = 0.f;
};
/** @brief One owning local part, independent of the source world and its runtime ids. */
struct BlueprintPart {
    BlueprintPartId          id;
    BuildingInstanceSnapshot local;
};
/**
 * @brief Versioned house recipe; admission validates all fields before use.
 * @ownership Owns local parts, exact curves and pinned definition values.
 * @thread Values may be transferred to workers; registries/worlds remain owner-thread only.
 */
struct BuildingBlueprint {
    eve::AssetGuid                       id;
    uint64_t                             version     = 1;
    float                                cellSize    = 1.f;
    float                                floorHeight = 3.f;
    std::vector<BlueprintPart>           parts;
    std::vector<codec::EdgeCurvePayload> curves;
    Value                                definitions;
};
/** @brief Owning house membership; poses remain authoritative on the world instances. */
struct BuildingAssembly {
    AssemblyId              id;
    eve::AssetGuid          blueprint;
    uint64_t                blueprintVersion = 1;
    AssemblyPose            frame;
    std::map<uint32_t, int> members;
    /** @brief Monotonic local identity; deleted part keys are never reused. */
    uint32_t nextPartKey = 1;
};
/**
 * @brief Move-only proof of a fully validated house operation.
 * @ownership Owns its candidate; destruction cancels without side effects.
 * @lifetime Independent of the world; commit rejects a destroyed/replaced owner's epoch.
 * @thread Owner-thread only; pure validation may run during preparation; observers run after commit.
 */
class EVENGINE_API_WORLD PreparedAssembly {
public:
    PreparedAssembly(PreparedAssembly&&) noexcept;
    PreparedAssembly& operator=(PreparedAssembly&&) noexcept;
    ~PreparedAssembly();
    PreparedAssembly(const PreparedAssembly&)            = delete;
    PreparedAssembly& operator=(const PreparedAssembly&) = delete;
    /** @brief Borrow the validated member preview until this token moves or is consumed. */
    std::span<const PlacedBuilding> preview() const;

private:
    friend class AssemblyService;
    friend Result<Value> executeHouseRequest(PlacementWorld& world, const Value& request);
    explicit PreparedAssembly(std::unique_ptr<PreparedAssemblyData> data);
    std::unique_ptr<PreparedAssemblyData> data_;
};
/** @brief Owning before/after domain states for exact, conflict-checked undo and redo. */
struct AssemblyReceipt {
    AssemblyId assembly;
    Value      before;
    Value      after;
};

/**
 * @brief Canonical house composition, blueprint and persistence operations.
 * @thread World/registry owner-thread only. Inputs are synchronous borrows, never retained.
 * @reentrancy Validation rules must be pure; committed events observe complete world state.
 * @ownership Worlds own members and assembly records; all results own their payloads.
 */
class EVENGINE_API_WORLD AssemblyService {
public:
    /**
     * @brief Capture an explicit, structurally closed selection relative to a grid vertex.
     * @cost Copies selected members and their definition values once per capture; never per frame.
     * @return Owning recipe or diagnostics; does not change the world or allocate instance ids.
     */
    [[nodiscard]] static Result<BuildingBlueprint> capture(const PlacementWorld& world, std::span<const int> members,
                                                           eve::AssetGuid id, AssemblyPose origin);
    /**
     * @brief Prepare one complete instance of a pinned blueprint in the target world.
     * @cost Copies world/house state once per pose change; dense free collisions can be quadratic; reuse preview.
     * @return Move-only commit token; any failure leaves world, ids and events untouched.
     */
    [[nodiscard]] static Result<PreparedAssembly> preparePlacement(const PlacementWorld&    world,
                                                                   const BuildingBlueprint& blueprint, AssemblyId id,
                                                                   AssemblyPose pose);
    /**
     * @brief Prepare a rigid move of the current members, including instance modifications.
     * @cost One whole-world candidate per completed gesture; not a per-frame setter.
     */
    [[nodiscard]] static Result<PreparedAssembly> prepareMove(const PlacementWorld& world, AssemblyId id,
                                                              AssemblyPose pose);
    /**
     * @brief Prepare removal of all house members, rejecting external structural dependents.
     * @cost One whole-world candidate per removal gesture.
     */
    [[nodiscard]] static Result<PreparedAssembly> prepareRemoval(const PlacementWorld& world, AssemblyId id);
    /**
     * @brief Prepare replacement of a house's complete member set for independent remodelling.
     * @param members Exact desired world-space members; zero instanceId allocates a new member.
     * @cost Copies a world and validates its final state once per edit gesture.
     */
    [[nodiscard]] static Result<PreparedAssembly> prepareRemodel(const PlacementWorld& world, AssemblyId id,
                                                                 std::span<const PlacedBuilding> members);
    /**
     * @brief Consume a prepared operation if world and definition preconditions still match.
     * @cost Compares owning world state once per commit, then publishes a single state exchange.
     * @return Exact reversible receipt; stale token returns Conflict without mutation.
     */
    [[nodiscard]] static Result<AssemblyReceipt> commit(PlacementWorld& world, PreparedAssembly token);
    /** @brief Query an owning membership snapshot, or NotFound; caller-thread only. */
    [[nodiscard]] static Result<BuildingAssembly> assembly(const PlacementWorld& world, AssemblyId id);
    /** @brief Resolve a handle only in its owning world/epoch/generation; owning result. */
    [[nodiscard]] static Result<BuildingAssembly> resolve(const PlacementWorld& world, AssemblyHandle handle);
    /** @brief Return a checked runtime handle for a currently live persistent house. */
    [[nodiscard]] static Result<AssemblyHandle> handle(const PlacementWorld& world, AssemblyId id);
    /** @brief Return a member's house identity, or no value for an ungrouped instance. */
    static std::optional<AssemblyId> owner(const PlacementWorld& world, int member);
    /** @brief Return an owning, stable-order house list for UI and persistence. */
    static std::vector<AssemblyId> assemblies(const PlacementWorld& world);
    /**
     * @brief Encode a blueprint in the versioned canonical schema.
     * @cost Copies and validates all parts and curve links once per save/export; curve matching can be quadratic.
     */
    [[nodiscard]] static Result<Value> encodeBlueprint(const BuildingBlueprint& blueprint);
    /**
     * @brief Validate and decode an untrusted blueprint without publishing state.
     * @cost Validates all parts/dependencies once per import; exact curve/member matching can be quadratic.
     */
    [[nodiscard]] static Result<BuildingBlueprint> decodeBlueprint(const Value& value);
    /**
     * @brief Capture the complete logical world, including grid, terrain, curves and assemblies.
     * @cost Linear owning copy of world cells and members, once per save or transaction boundary.
     */
    [[nodiscard]] static Result<Value> snapshotWorld(const PlacementWorld& world);
    /**
     * @brief Validate and stage a complete saved world; failure preserves the old world.
     * @cost Builds a complete replacement world once per load; all old runtime handles expire.
     */
    [[nodiscard]] static Result<void> restoreWorld(PlacementWorld& world, const Value& value);
    /**
     * @brief Apply a receipt endpoint only when the current world matches the expected endpoint.
     * @cost Complete world comparison and candidate restore, once per undo/redo.
     */
    [[nodiscard]] static Result<void> applyReceipt(PlacementWorld& world, const Value& expected, const Value& desired);

private:
    static Value                    admissionStamp(const PlacementWorld& world);
    static Result<PreparedAssembly> prepareState(const PlacementWorld& world, std::unique_ptr<PlacementWorld> candidate,
                                                 AssemblyId id);
    static Result<void>             validateFinal(PlacementWorld& world);
    static Result<std::unique_ptr<PlacementWorld>> decodeWorld(const Value& value);
};
}  // namespace eve::building
