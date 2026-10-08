#pragma once

#include "common/Result.h"

#include <array>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Horizontal or vertical face used by grid-module connection constraints. */
enum class ModuleFacing : unsigned char { North, East, South, West, Up, Down };

/** @brief A typed connection exposed on one occupied cell edge. */
struct ModuleConnector {
    int          cellX  = 0;
    int          cellZ  = 0;
    ModuleFacing facing = ModuleFacing::North;
    std::string  tag;
    std::string  accepts;
    int          levelOffset = 0;
};

/** @brief Immutable footprint and connection contract for one modular asset. */
struct ModuleDefinition {
    std::string                  id;
    int                          widthCells = 1;
    int                          depthCells = 1;
    std::vector<ModuleConnector> connectors;
    int                          heightLevels = 1;
};

/** @brief World-grid placement accepted by a ModuleAssemblyPlan. */
struct ModulePlacement {
    std::string moduleId;
    int         cellX       = 0;
    int         cellZ       = 0;
    int         level       = 0;
    int         quarterTurn = 0;
};

/** @brief Configurable limits shared by automatic modular-map generators. */
struct ModuleAssemblyConstraints {
    float               cellSize    = 4.0f;
    float               floorHeight = 4.0f;
    int                 minCellX    = 0;
    int                 maxCellX    = 0;
    int                 minCellZ    = 0;
    int                 maxCellZ    = 0;
    int                 maxLevels   = 1;
    std::array<bool, 4> allowedQuarterTurns{true, true, true, true};
    bool                bounded             = false;
    bool                requireConnection   = true;
    float               minimumSupportRatio = 0.0f;
};

/**
 * @brief Deterministic owner of a validated modular assembly plan.
 *
 * Definitions are registered before placement. A placement is committed only
 * after its rotated footprint, bounds, occupancy and every touching connector
 * have been validated. Failed placements leave the plan unchanged.
 *
 * @ownership Owns definitions and placements by value.
 * @thread Affine to the caller; external synchronization is required.
 * @reentrancy Does not invoke callbacks.
 */
class ModuleAssemblyPlan {
public:
    /** @brief Construct an empty plan with default constraints. */
    ModuleAssemblyPlan() = default;
    /** @brief Construct an empty plan with a copied constraint configuration. */
    explicit ModuleAssemblyPlan(ModuleAssemblyConstraints constraints);

    /** @brief Configure an empty plan before definitions or placements are added. */
    [[nodiscard]] Result<void> configure(float cellSize, float floorHeight, int minCellX, int maxCellX, int minCellZ,
                                         int maxCellZ, int maxLevels, bool bounded, bool requireConnection);

    /** @brief Configure the four permitted quarter-turn rotations before registration or placement. */
    [[nodiscard]] Result<void> setAllowedQuarterTurns(bool turn0, bool turn90, bool turn180, bool turn270);

    /**
     * @brief Require this fraction of an elevated module's bottom cells to be supported from below.
     *
     * A value of zero permits cantilevered modules; one requires every bottom cell to have an occupied
     * cell directly beneath it. Touching vertical faces still require compatible Up/Down connectors.
     */
    [[nodiscard]] Result<void> setMinimumSupportRatio(float ratio);

    /** @brief Atomically replace an empty plan's constraints and module definitions from strict JSON.
     * Schema `eve.procgen.module-assembly`, version 1 rejects unknown fields. Placements are never restored.
     * @param json UTF-8 configuration, limited to 1 MiB, owning no external references.
     * @return Parse/validation failure without mutation, or an applied empty configured plan.
     * @thread Caller-affine, synchronous, callback-free.
     * @cost Linear in JSON bytes plus module and connector count; intended for one-time setup.
     */
    [[nodiscard]] Result<void> applyConfigJson(const std::string& json);

    /** @brief Register a module footprint using scalar script-friendly fields. */
    [[nodiscard]] Result<void> registerModuleType(std::string id, int widthCells, int depthCells);

    /** @brief Register a three-dimensional module volume using scalar script-friendly fields. */
    [[nodiscard]] Result<void> registerVolumeModuleType(std::string id, int widthCells, int depthCells,
                                                        int heightLevels);

    /** @brief Append a typed connector to a registered, not-yet-placed module. */
    [[nodiscard]] Result<void> addConnector(const std::string& moduleId, int cellX, int cellZ, int facing,
                                            std::string tag, std::string accepts);

    /** @brief Append a typed connector to one face of a cell in a module volume. */
    [[nodiscard]] Result<void> addVolumeConnector(const std::string& moduleId, int cellX, int cellZ, int levelOffset,
                                                  int facing, std::string tag, std::string accepts);

    /** @brief Register one module definition before it is placed. */
    [[nodiscard]] Result<void> registerModule(ModuleDefinition definition);

    /**
     * @brief Validate and atomically append one placement.
     * @cost Linear in existing occupied cells and connectors.
     */
    [[nodiscard]] Result<void> place(ModulePlacement placement);

    /** @brief Borrow the accepted placements in deterministic insertion order. */
    [[nodiscard]] const std::vector<ModulePlacement>& placements() const noexcept { return placements_; }
    /** @brief Return the number of accepted placements. */
    [[nodiscard]] int getPlacementCount() const noexcept { return static_cast<int>(placements_.size()); }
    /** @brief Return one accepted placement's module id, or empty text for an invalid index. */
    [[nodiscard]] std::string getPlacementModule(int index) const;
    /** @brief Return one placement's world X coordinate, or zero for an invalid index. */
    [[nodiscard]] float getPlacementX(int index) const noexcept;
    /** @brief Return one placement's world Y coordinate, or zero for an invalid index. */
    [[nodiscard]] float getPlacementY(int index) const noexcept;
    /** @brief Return one placement's world Z coordinate, or zero for an invalid index. */
    [[nodiscard]] float getPlacementZ(int index) const noexcept;
    /** @brief Return one placement's yaw in degrees, or zero for an invalid index. */
    [[nodiscard]] float getPlacementYawDegrees(int index) const noexcept;
    /** @brief Return the immutable constraint configuration. */
    [[nodiscard]] const ModuleAssemblyConstraints& constraints() const noexcept { return constraints_; }

private:
    ModuleAssemblyConstraints     constraints_;
    std::vector<ModuleDefinition> definitions_;
    std::vector<ModulePlacement>  placements_;
};

}  // namespace eve::procgen
