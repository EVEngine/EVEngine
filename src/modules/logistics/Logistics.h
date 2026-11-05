#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "common/Time.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::logistics {

/** @brief Orthographic belt / port facing on the 2D factory grid (Y grows south). */
enum class Dir : std::uint8_t { East = 0, South = 1, West = 2, North = 3 };

/** @brief Occupancy kind stored per grid cell. */
enum class CellKind : std::uint8_t { Empty = 0, Belt = 1, Machine = 2, Source = 3, Sink = 4 };

/** @brief Compact recipe: consume `inputCount` of `inputType`, emit `outputCount` of `outputType`. */
struct Recipe {
    std::string id;
    int         inputType   = 0;
    int         inputCount  = 1;
    int         outputType  = 0;
    int         outputCount = 1;
    float       durationSec = 1.0f;
};

/** @brief One completed fixed logistics step. */
struct StepReport {
    SimulationTick tick;
    Duration       elapsed;
    int            itemsMoved     = 0;
    int            craftsStarted  = 0;
    int            craftsFinished = 0;
    int            itemsSpawned   = 0;
    int            itemsSunk      = 0;
    int            jammedMoves    = 0;
};

/** @brief Read-only item snapshot for script / GPU packing. */
struct ItemState {
    float x        = 0.f;  ///< World X (cell center + progress along belt).
    float y        = 0.f;  ///< World Y.
    int   type     = 0;
    int   cellX    = 0;
    int   cellY    = 0;
    float progress = 0.f;
    int   dir      = 0;
};

/** @brief Read-only machine snapshot. */
struct MachineState {
    int   id            = -1;
    int   originX       = 0;
    int   originY       = 0;
    int   width         = 1;
    int   height        = 1;
    int   recipeIndex   = -1;
    int   inputStored   = 0;
    int   outputStored  = 0;
    float craftProgress = 0.f;
    bool  crafting      = false;
    int   layer         = 0;
};

/**
 * @brief Deterministic 2D factory logistics: belts, discrete items, machine buffers, crafts.
 *
 * Grid cells are the authority for belt topology. Items are a dense SoA (not one ECS entity
 * per cargo). Machines own input/output buffers and an optional in-module craft timer; they
 * expose `machineOwnerId` so a game can project the same work onto `production` WorkQueues.
 *
 * Layer 0 is the only active floor in the vertical slice; `configure` accepts `layers` so a
 * later multi-floor / 3D host can reuse the same cell addressing `(x,y,layer)`.
 *
 * @thread Simulation owner thread only.
 * @reentrancy No callbacks; no graphics includes.
 */
class EVENGINE_API_FOUNDATION Logistics : public Module {
public:
    Module_REG(Logistics);
    /** @brief Construct an empty unconfigured logistics world. */
    Logistics();
    /** @brief Destroy logistics storage. */
    ~Logistics() override;

    /**
     * @brief Size the factory grid and world mapping.
     * @param width Cell count on X (>0).
     * @param height Cell count on Y (>0).
     * @param layers Floor count (>=1); only layer 0 is stepped in this slice.
     * @param cellSize World units per cell (>0).
     * @param originX World X of cell (0,0) top-left corner.
     * @param originY World Y of cell (0,0) top-left corner.
     * @return InvalidArgument for non-positive sizes; Applied on success (clears prior state).
     * @cost Allocates O(width*height*layers) cell storage; amortize at factory rebuild.
     */
    [[nodiscard]] Result<void> configure(int width, int height, int layers, float cellSize, float originX,
                                         float originY);

    /** @brief Clear every cell, item, machine, recipe and reset the simulation clock. */
    void clear();

    /** @brief Register or replace a named recipe. @return recipe index. */
    [[nodiscard]] Result<int> registerRecipe(std::string id, int inputType, int inputCount, int outputType,
                                             int outputCount, float durationSec);

    /** @brief Place a belt cell facing `dir` (0=E,1=S,2=W,3=N) on layer. */
    [[nodiscard]] Result<void> placeBelt(int cx, int cy, int dir, int layer = 0);

    /** @brief Remove belt/source/sink content from a cell (machines must use removeMachine). */
    [[nodiscard]] Result<void> clearCell(int cx, int cy, int layer = 0);

    /**
     * @brief Place a rectangular machine footprint and bind a recipe.
     * @return Stable machine id (>=0).
     * @remarks Input port faces west of origin; output port faces east of origin+width-1.
     */
    [[nodiscard]] Result<int> placeMachine(int cx, int cy, int width, int height, std::string_view recipeId,
                                           int layer = 0);

    /** @brief Remove a machine by id and free its footprint cells. */
    [[nodiscard]] Result<void> removeMachine(int machineId);

    /** @brief Place an item emitter that injects `itemType` onto the eastward adjacent belt. */
    [[nodiscard]] Result<void> placeSource(int cx, int cy, int itemType, float itemsPerSec, int layer = 0);

    /** @brief Place a sink that destroys items arriving from the westward adjacent belt. */
    [[nodiscard]] Result<void> placeSink(int cx, int cy, int layer = 0);

    /** @brief Spawn one item onto a belt cell at progress 0. */
    [[nodiscard]] Result<int> spawnItem(int cx, int cy, int itemType, int layer = 0);

    /**
     * @brief Advance one fixed logistics step.
     * @param step External tick must be strictly increasing; dt must be positive and finite.
     * @cost O(items + machines + sources); amortize once per simulation tick.
     */
    [[nodiscard]] Result<StepReport> advance(const SimulationStep& step);

    /**
     * @brief Convenience advance that allocates the next tick from an internal counter.
     * @remarks Prefer `advance(SimulationStep)` for replay; this path is for demos/tests.
     */
    [[nodiscard]] Result<StepReport> advance(float dt);

    /** @brief Grid width in cells. */
    [[nodiscard]] int width() const noexcept { return width_; }
    /** @brief Grid height in cells. */
    [[nodiscard]] int height() const noexcept { return height_; }
    /** @brief Configured floor count. */
    [[nodiscard]] int layers() const noexcept { return layers_; }
    /** @brief World units per cell. */
    [[nodiscard]] float cellSize() const noexcept { return cellSize_; }
    /** @brief World origin X. */
    [[nodiscard]] float originX() const noexcept { return originX_; }
    /** @brief World origin Y. */
    [[nodiscard]] float originY() const noexcept { return originY_; }
    /** @brief Last accepted simulation tick. */
    [[nodiscard]] SimulationTick currentTick() const noexcept { return tick_; }

    /** @brief Cell kind at (cx,cy,layer); Empty when out of range. */
    [[nodiscard]] int cellKind(int cx, int cy, int layer = 0) const noexcept;
    /** @brief Belt direction at cell, or -1 when not a belt. */
    [[nodiscard]] int cellDir(int cx, int cy, int layer = 0) const noexcept;
    /** @brief Machine id occupying the cell, or -1. */
    [[nodiscard]] int cellMachine(int cx, int cy, int layer = 0) const noexcept;

    /** @brief Live item count. */
    [[nodiscard]] int itemCount() const noexcept { return static_cast<int>(itemType_.size()); }
    /** @brief Copy item i; NotFound when index is out of range. */
    [[nodiscard]] Result<ItemState> itemAt(int index) const;
    /**
     * @brief Pack GPU-friendly floats: [cellX,cellY,progress,dir,type,layer] * itemCount.
     * @cost O(items); amortize once per upload.
     */
    [[nodiscard]] Result<std::vector<float>> packItemSimFloats() const;
    /**
     * @brief Pack world positions for CPU draw fallback: [x,y,type] * itemCount.
     * @cost O(items).
     */
    [[nodiscard]] Result<std::vector<float>> packItemWorldFloats() const;

    /** @brief Live machine count (includes holes after remove; use machineAt). */
    [[nodiscard]] int machineSlotCount() const noexcept { return static_cast<int>(machines_.size()); }
    /** @brief Copy machine slot i; NotFound when empty/out of range. */
    [[nodiscard]] Result<MachineState> machineAt(int index) const;
    /** @brief Stable Production-owner string for machine slot i (empty when invalid). */
    [[nodiscard]] std::string machineOwnerId(int index) const;

    /** @brief Items destroyed by sinks since configure/clear. */
    [[nodiscard]] int sunkTotal() const noexcept { return sunkTotal_; }
    /** @brief Crafts completed since configure/clear. */
    [[nodiscard]] int craftsTotal() const noexcept { return craftsTotal_; }

    /** @brief Default belt speed in cells per second. */
    void setBeltSpeed(float cellsPerSec) noexcept { beltSpeed_ = cellsPerSec > 0.f ? cellsPerSec : beltSpeed_; }
    /** @brief Current belt speed. */
    [[nodiscard]] float beltSpeed() const noexcept { return beltSpeed_; }
    /** @brief Max items stored in a machine input or output buffer. */
    void setBufferCapacity(int capacity) noexcept { bufferCapacity_ = capacity > 0 ? capacity : bufferCapacity_; }
    /** @brief Current buffer capacity. */
    [[nodiscard]] int bufferCapacity() const noexcept { return bufferCapacity_; }

private:
    struct Cell {
        CellKind kind        = CellKind::Empty;
        Dir      dir         = Dir::East;
        int      machineSlot = -1;
        int      itemType    = 0;    ///< Source spawn type.
        float    spawnRate   = 0.f;  ///< Source items/sec.
        float    spawnAcc    = 0.f;
    };

    struct Machine {
        bool        alive         = false;
        int         id            = -1;
        int         originX       = 0;
        int         originY       = 0;
        int         width         = 1;
        int         height        = 1;
        int         layer         = 0;
        int         recipeIndex   = -1;
        int         inputStored   = 0;
        int         outputStored  = 0;
        float       craftProgress = 0.f;
        bool        crafting      = false;
        std::string ownerId;
    };

    struct Item {
        int   cellX    = 0;
        int   cellY    = 0;
        int   layer    = 0;
        int   type     = 0;
        float progress = 0.f;
        Dir   dir      = Dir::East;
    };

    [[nodiscard]] bool         inBounds(int cx, int cy, int layer) const noexcept;
    [[nodiscard]] std::size_t  cellIndex(int cx, int cy, int layer) const noexcept;
    static void                dirDelta(Dir dir, int& dx, int& dy) noexcept;
    [[nodiscard]] ItemState    makeItemState(const Item& item) const noexcept;
    [[nodiscard]] Result<void> ensureConfigured() const;
    void                       destroyItemAt(std::size_t index);
    [[nodiscard]] int          findItemAt(int cx, int cy, int layer) const noexcept;
    void                       stepBelts(float dt, StepReport& report);
    void                       stepSources(float dt, StepReport& report);
    void                       stepSinks(StepReport& report);
    void                       stepMachines(float dt, StepReport& report);
    void                       tryPullInputs();
    void                       tryPushOutputs();

    int            width_          = 0;
    int            height_         = 0;
    int            layers_         = 0;
    float          cellSize_       = 16.f;
    float          originX_        = 0.f;
    float          originY_        = 0.f;
    float          beltSpeed_      = 4.f;
    int            bufferCapacity_ = 8;
    int            nextMachineId_  = 1;
    int            sunkTotal_      = 0;
    int            craftsTotal_    = 0;
    SimulationTick tick_           = SimulationTick::zero();

    std::vector<Cell>    cells_;
    std::vector<Machine> machines_;
    std::vector<Recipe>  recipes_;
    // Item SoA (dense).
    std::vector<int>          itemType_;
    std::vector<int>          itemCellX_;
    std::vector<int>          itemCellY_;
    std::vector<int>          itemLayer_;
    std::vector<float>        itemProgress_;
    std::vector<std::uint8_t> itemDir_;
    std::vector<std::int32_t> occupancy_;  ///< Per-cell item index or -1.
};

}  // namespace eve::logistics
