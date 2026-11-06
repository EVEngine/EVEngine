// eve-hot-path
#include "logistics/Logistics.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::logistics {
namespace {

Result<void> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
Result<T> failT(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

Logistics::Logistics()  = default;
Logistics::~Logistics() = default;

Module_IMPL(Logistics, new Logistics());

Result<void> Logistics::configure(int width, int height, int layers, float cellSize, float originX, float originY) {
    if (width <= 0 || height <= 0 || layers <= 0)
        return fail(DiagnosticCode::InvalidArgument, "grid dimensions must be positive", "configure");
    if (!(cellSize > 0.f) || !std::isfinite(cellSize))
        return fail(DiagnosticCode::InvalidArgument, "cellSize must be a positive finite float", "cellSize");
    if (!std::isfinite(originX) || !std::isfinite(originY))
        return fail(DiagnosticCode::InvalidArgument, "origin must be finite", "origin");
    width_    = width;
    height_   = height;
    layers_   = layers;
    cellSize_ = cellSize;
    originX_  = originX;
    originY_  = originY;
    cells_.assign(
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * static_cast<std::size_t>(layers_),
        Cell{});
    machines_.clear();
    recipes_.clear();
    itemType_.clear();
    itemCellX_.clear();
    itemCellY_.clear();
    itemLayer_.clear();
    itemProgress_.clear();
    itemDir_.clear();
    occupancy_.assign(cells_.size(), -1);
    nextMachineId_ = 1;
    sunkTotal_     = 0;
    craftsTotal_   = 0;
    tick_          = SimulationTick::zero();
    return Result<void>::success();
}

void Logistics::clear() {
    if (width_ <= 0) return;
    std::fill(cells_.begin(), cells_.end(), Cell{});
    machines_.clear();
    itemType_.clear();
    itemCellX_.clear();
    itemCellY_.clear();
    itemLayer_.clear();
    itemProgress_.clear();
    itemDir_.clear();
    occupancy_.assign(cells_.size(), -1);
    nextMachineId_ = 1;
    sunkTotal_     = 0;
    craftsTotal_   = 0;
    tick_          = SimulationTick::zero();
}

Result<void> Logistics::ensureConfigured() const {
    if (width_ <= 0 || height_ <= 0 || layers_ <= 0)
        return fail(DiagnosticCode::InvalidArgument, "logistics grid is not configured", "configure");
    return Result<void>::success();
}

bool Logistics::inBounds(int cx, int cy, int layer) const noexcept {
    return cx >= 0 && cy >= 0 && layer >= 0 && cx < width_ && cy < height_ && layer < layers_;
}

std::size_t Logistics::cellIndex(int cx, int cy, int layer) const noexcept {
    return (static_cast<std::size_t>(layer) * static_cast<std::size_t>(height_) + static_cast<std::size_t>(cy)) *
               static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(cx);
}

void Logistics::dirDelta(Dir dir, int& dx, int& dy) noexcept {
    switch (dir) {
        case Dir::East:
            dx = 1;
            dy = 0;
            break;
        case Dir::South:
            dx = 0;
            dy = 1;
            break;
        case Dir::West:
            dx = -1;
            dy = 0;
            break;
        case Dir::North:
            dx = 0;
            dy = -1;
            break;
    }
}

Result<int> Logistics::registerRecipe(std::string id, int inputType, int inputCount, int outputType, int outputCount,
                                      float durationSec) {
    if (id.empty()) return failT<int>(DiagnosticCode::InvalidArgument, "recipe id must be non-empty", "id");
    if (inputCount <= 0 || outputCount <= 0)
        return failT<int>(DiagnosticCode::InvalidArgument, "recipe counts must be positive", "count");
    if (!(durationSec > 0.f) || !std::isfinite(durationSec))
        return failT<int>(DiagnosticCode::InvalidArgument, "recipe duration must be positive finite", "duration");
    for (std::size_t i = 0; i < recipes_.size(); ++i) {
        if (recipes_[i].id == id) {
            recipes_[i] = Recipe{std::move(id), inputType, inputCount, outputType, outputCount, durationSec};
            return Result<int>::success(static_cast<int>(i));
        }
    }
    recipes_.push_back(Recipe{std::move(id), inputType, inputCount, outputType, outputCount, durationSec});
    return Result<int>::success(static_cast<int>(recipes_.size() - 1));
}

Result<void> Logistics::placeBelt(int cx, int cy, int dir, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return ready;
    if (!inBounds(cx, cy, layer)) return fail(DiagnosticCode::InvalidArgument, "cell out of bounds", "placeBelt");
    if (dir < 0 || dir > 3) return fail(DiagnosticCode::InvalidArgument, "dir must be 0..3", "dir");
    Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind == CellKind::Machine)
        return fail(DiagnosticCode::Conflict, "cannot place belt on machine footprint", "placeBelt");
    cell.kind        = CellKind::Belt;
    cell.dir         = static_cast<Dir>(dir);
    cell.machineSlot = -1;
    cell.itemType    = 0;
    cell.spawnRate   = 0.f;
    cell.spawnAcc    = 0.f;
    return Result<void>::success();
}

Result<void> Logistics::clearCell(int cx, int cy, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return ready;
    if (!inBounds(cx, cy, layer)) return fail(DiagnosticCode::InvalidArgument, "cell out of bounds", "clearCell");
    Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind == CellKind::Machine)
        return fail(DiagnosticCode::Conflict, "use removeMachine for machine footprints", "clearCell");
    const int item = findItemAt(cx, cy, layer);
    if (item >= 0) destroyItemAt(static_cast<std::size_t>(item));
    cell = Cell{};
    return Result<void>::success();
}

Result<int> Logistics::placeMachine(int cx, int cy, int width, int height, std::string_view recipeId, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return Result<int>::failure(ready.status());
    if (width <= 0 || height <= 0)
        return failT<int>(DiagnosticCode::InvalidArgument, "machine size must be positive", "size");
    if (!inBounds(cx, cy, layer) || !inBounds(cx + width - 1, cy + height - 1, layer))
        return failT<int>(DiagnosticCode::InvalidArgument, "machine footprint out of bounds", "placeMachine");
    int recipeIndex = -1;
    for (std::size_t i = 0; i < recipes_.size(); ++i) {
        if (recipes_[i].id == recipeId) {
            recipeIndex = static_cast<int>(i);
            break;
        }
    }
    if (recipeIndex < 0) return failT<int>(DiagnosticCode::NotFound, "recipe id is not registered", "recipeId");
    for (int y = cy; y < cy + height; ++y) {
        for (int x = cx; x < cx + width; ++x) {
            if (cells_[cellIndex(x, y, layer)].kind != CellKind::Empty)
                return failT<int>(DiagnosticCode::Conflict, "machine footprint overlaps occupied cells",
                                  "placeMachine");
            if (findItemAt(x, y, layer) >= 0)
                return failT<int>(DiagnosticCode::Conflict, "machine footprint overlaps items", "placeMachine");
        }
    }
    Machine machine;
    machine.alive       = true;
    machine.id          = nextMachineId_++;
    machine.originX     = cx;
    machine.originY     = cy;
    machine.width       = width;
    machine.height      = height;
    machine.layer       = layer;
    machine.recipeIndex = recipeIndex;
    machine.ownerId     = "machine:" + std::to_string(machine.id);
    const int slot      = static_cast<int>(machines_.size());
    machines_.push_back(std::move(machine));
    for (int y = cy; y < cy + height; ++y) {
        for (int x = cx; x < cx + width; ++x) {
            Cell& cell       = cells_[cellIndex(x, y, layer)];
            cell.kind        = CellKind::Machine;
            cell.machineSlot = slot;
        }
    }
    return Result<int>::success(machines_.back().id);
}

Result<void> Logistics::removeMachine(int machineId) {
    if (auto ready = ensureConfigured(); !ready.ok()) return ready;
    for (std::size_t i = 0; i < machines_.size(); ++i) {
        Machine& machine = machines_[i];
        if (!machine.alive || machine.id != machineId) continue;
        for (int y = machine.originY; y < machine.originY + machine.height; ++y) {
            for (int x = machine.originX; x < machine.originX + machine.width; ++x) {
                cells_[cellIndex(x, y, machine.layer)] = Cell{};
            }
        }
        machine = Machine{};
        return Result<void>::success();
    }
    return fail(DiagnosticCode::NotFound, "machine id not found", "removeMachine");
}

Result<void> Logistics::placeSource(int cx, int cy, int itemType, float itemsPerSec, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return ready;
    if (!inBounds(cx, cy, layer)) return fail(DiagnosticCode::InvalidArgument, "cell out of bounds", "placeSource");
    if (!(itemsPerSec > 0.f) || !std::isfinite(itemsPerSec))
        return fail(DiagnosticCode::InvalidArgument, "itemsPerSec must be positive finite", "itemsPerSec");
    Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind == CellKind::Machine)
        return fail(DiagnosticCode::Conflict, "cannot place source on machine footprint", "placeSource");
    cell.kind        = CellKind::Source;
    cell.itemType    = itemType;
    cell.spawnRate   = itemsPerSec;
    cell.spawnAcc    = 0.f;
    cell.dir         = Dir::East;
    cell.machineSlot = -1;
    return Result<void>::success();
}

Result<void> Logistics::placeSink(int cx, int cy, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return ready;
    if (!inBounds(cx, cy, layer)) return fail(DiagnosticCode::InvalidArgument, "cell out of bounds", "placeSink");
    Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind == CellKind::Machine)
        return fail(DiagnosticCode::Conflict, "cannot place sink on machine footprint", "placeSink");
    cell.kind        = CellKind::Sink;
    cell.machineSlot = -1;
    cell.itemType    = 0;
    cell.spawnRate   = 0.f;
    cell.spawnAcc    = 0.f;
    return Result<void>::success();
}

int Logistics::findItemAt(int cx, int cy, int layer) const noexcept {
    if (!inBounds(cx, cy, layer) || occupancy_.empty()) return -1;
    return occupancy_[cellIndex(cx, cy, layer)];
}

void Logistics::destroyItemAt(std::size_t index) {
    EV_ASSERT(index < itemType_.size());
    const int cx    = itemCellX_[index];
    const int cy    = itemCellY_[index];
    const int layer = itemLayer_[index];
    if (inBounds(cx, cy, layer)) occupancy_[cellIndex(cx, cy, layer)] = -1;
    const std::size_t last = itemType_.size() - 1;
    if (index != last) {
        itemType_[index]     = itemType_[last];
        itemCellX_[index]    = itemCellX_[last];
        itemCellY_[index]    = itemCellY_[last];
        itemLayer_[index]    = itemLayer_[last];
        itemProgress_[index] = itemProgress_[last];
        itemDir_[index]      = itemDir_[last];
        if (inBounds(itemCellX_[index], itemCellY_[index], itemLayer_[index]))
            occupancy_[cellIndex(itemCellX_[index], itemCellY_[index], itemLayer_[index])] =
                static_cast<std::int32_t>(index);
    }
    itemType_.pop_back();
    itemCellX_.pop_back();
    itemCellY_.pop_back();
    itemLayer_.pop_back();
    itemProgress_.pop_back();
    itemDir_.pop_back();
}

Result<int> Logistics::spawnItem(int cx, int cy, int itemType, int layer) {
    if (auto ready = ensureConfigured(); !ready.ok()) return Result<int>::failure(ready.status());
    if (!inBounds(cx, cy, layer)) return failT<int>(DiagnosticCode::InvalidArgument, "cell out of bounds", "spawnItem");
    const Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind != CellKind::Belt)
        return failT<int>(DiagnosticCode::Conflict, "items may only spawn on belt cells", "spawnItem");
    if (findItemAt(cx, cy, layer) >= 0)
        return failT<int>(DiagnosticCode::Conflict, "belt cell already occupied", "spawnItem");
    const int index = static_cast<int>(itemType_.size());
    itemType_.push_back(itemType);
    itemCellX_.push_back(cx);
    itemCellY_.push_back(cy);
    itemLayer_.push_back(layer);
    itemProgress_.push_back(0.f);
    itemDir_.push_back(static_cast<std::uint8_t>(cell.dir));
    occupancy_[cellIndex(cx, cy, layer)] = index;
    return Result<int>::success(index);
}

ItemState Logistics::makeItemState(const Item& item) const noexcept {
    ItemState state;
    state.cellX    = item.cellX;
    state.cellY    = item.cellY;
    state.type     = item.type;
    state.progress = item.progress;
    state.dir      = static_cast<int>(item.dir);
    int dx = 0, dy = 0;
    dirDelta(item.dir, dx, dy);
    const float along = item.progress * cellSize_;
    state.x           = originX_ + (static_cast<float>(item.cellX) + 0.5f) * cellSize_ + static_cast<float>(dx) * along;
    state.y           = originY_ + (static_cast<float>(item.cellY) + 0.5f) * cellSize_ + static_cast<float>(dy) * along;
    return state;
}

Result<ItemState> Logistics::itemAt(int index) const {
    if (index < 0 || index >= itemCount())
        return failT<ItemState>(DiagnosticCode::NotFound, "item index out of range", "itemAt");
    Item item;
    item.cellX    = itemCellX_[static_cast<std::size_t>(index)];
    item.cellY    = itemCellY_[static_cast<std::size_t>(index)];
    item.layer    = itemLayer_[static_cast<std::size_t>(index)];
    item.type     = itemType_[static_cast<std::size_t>(index)];
    item.progress = itemProgress_[static_cast<std::size_t>(index)];
    item.dir      = static_cast<Dir>(itemDir_[static_cast<std::size_t>(index)]);
    return Result<ItemState>::success(makeItemState(item));
}

Result<std::vector<float>> Logistics::packItemSimFloats() const {
    std::vector<float> out;
    out.resize(static_cast<std::size_t>(itemCount()) * 6u);
    for (int i = 0; i < itemCount(); ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * 6u;
        out[base + 0]          = static_cast<float>(itemCellX_[static_cast<std::size_t>(i)]);
        out[base + 1]          = static_cast<float>(itemCellY_[static_cast<std::size_t>(i)]);
        out[base + 2]          = itemProgress_[static_cast<std::size_t>(i)];
        out[base + 3]          = static_cast<float>(itemDir_[static_cast<std::size_t>(i)]);
        out[base + 4]          = static_cast<float>(itemType_[static_cast<std::size_t>(i)]);
        out[base + 5]          = static_cast<float>(itemLayer_[static_cast<std::size_t>(i)]);
    }
    return Result<std::vector<float>>::success(std::move(out));
}

Result<std::vector<float>> Logistics::packItemWorldFloats() const {
    std::vector<float> out;
    out.resize(static_cast<std::size_t>(itemCount()) * 3u);
    for (int i = 0; i < itemCount(); ++i) {
        auto state = itemAt(i);
        if (!state.ok()) {
            state.ignore("packItemWorldFloats skipped missing item");
            continue;
        }
        const ItemState&  snap = state.value();
        const std::size_t base = static_cast<std::size_t>(i) * 3u;
        out[base + 0]          = snap.x;
        out[base + 1]          = snap.y;
        out[base + 2]          = static_cast<float>(snap.type);
    }
    return Result<std::vector<float>>::success(std::move(out));
}

Result<MachineState> Logistics::machineAt(int index) const {
    if (index < 0 || index >= static_cast<int>(machines_.size()))
        return failT<MachineState>(DiagnosticCode::NotFound, "machine slot out of range", "machineAt");
    const Machine& machine = machines_[static_cast<std::size_t>(index)];
    if (!machine.alive) return failT<MachineState>(DiagnosticCode::NotFound, "machine slot is empty", "machineAt");
    MachineState out;
    out.id            = machine.id;
    out.originX       = machine.originX;
    out.originY       = machine.originY;
    out.width         = machine.width;
    out.height        = machine.height;
    out.recipeIndex   = machine.recipeIndex;
    out.inputStored   = machine.inputStored;
    out.outputStored  = machine.outputStored;
    out.craftProgress = machine.craftProgress;
    out.crafting      = machine.crafting;
    out.layer         = machine.layer;
    return Result<MachineState>::success(out);
}

std::string Logistics::machineOwnerId(int index) const {
    if (index < 0 || index >= static_cast<int>(machines_.size())) return {};
    const Machine& machine = machines_[static_cast<std::size_t>(index)];
    return machine.alive ? machine.ownerId : std::string{};
}

int Logistics::cellKind(int cx, int cy, int layer) const noexcept {
    if (!inBounds(cx, cy, layer)) return static_cast<int>(CellKind::Empty);
    return static_cast<int>(cells_[cellIndex(cx, cy, layer)].kind);
}

int Logistics::cellDir(int cx, int cy, int layer) const noexcept {
    if (!inBounds(cx, cy, layer)) return -1;
    const Cell& cell = cells_[cellIndex(cx, cy, layer)];
    return cell.kind == CellKind::Belt ? static_cast<int>(cell.dir) : -1;
}

int Logistics::cellMachine(int cx, int cy, int layer) const noexcept {
    if (!inBounds(cx, cy, layer)) return -1;
    const Cell& cell = cells_[cellIndex(cx, cy, layer)];
    if (cell.kind != CellKind::Machine || cell.machineSlot < 0) return -1;
    const Machine& machine = machines_[static_cast<std::size_t>(cell.machineSlot)];
    return machine.alive ? machine.id : -1;
}

void Logistics::stepSources(float dt, StepReport& report) {
    for (int layer = 0; layer < layers_; ++layer) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Cell& cell = cells_[cellIndex(x, y, layer)];
                if (cell.kind != CellKind::Source) continue;
                cell.spawnAcc += cell.spawnRate * dt;
                while (cell.spawnAcc >= 1.f) {
                    const int tx = x + 1;
                    const int ty = y;
                    if (!inBounds(tx, ty, layer) || cells_[cellIndex(tx, ty, layer)].kind != CellKind::Belt) break;
                    if (findItemAt(tx, ty, layer) >= 0) break;
                    auto spawned = spawnItem(tx, ty, cell.itemType, layer);
                    if (!spawned.ok()) {
                        spawned.ignore("source spawn rejected");
                        break;
                    }
                    cell.spawnAcc -= 1.f;
                    ++report.itemsSpawned;
                }
            }
        }
    }
}

void Logistics::stepSinks(StepReport& report) {
    for (int layer = 0; layer < layers_; ++layer) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (cells_[cellIndex(x, y, layer)].kind != CellKind::Sink) continue;
                const int sx = x - 1;
                const int sy = y;
                if (!inBounds(sx, sy, layer)) continue;
                const int item = findItemAt(sx, sy, layer);
                if (item < 0) continue;
                // Pull when item is near the end of the west belt cell heading east.
                if (static_cast<Dir>(itemDir_[static_cast<std::size_t>(item)]) != Dir::East) continue;
                if (itemProgress_[static_cast<std::size_t>(item)] < 0.85f) continue;
                destroyItemAt(static_cast<std::size_t>(item));
                ++sunkTotal_;
                ++report.itemsSunk;
            }
        }
    }
}

void Logistics::tryPullInputs() {
    for (Machine& machine : machines_) {
        if (!machine.alive || machine.recipeIndex < 0) continue;
        const Recipe& recipe = recipes_[static_cast<std::size_t>(machine.recipeIndex)];
        if (machine.inputStored >= bufferCapacity_) continue;
        const int px = machine.originX - 1;
        const int py = machine.originY;
        if (!inBounds(px, py, machine.layer)) continue;
        const int item = findItemAt(px, py, machine.layer);
        if (item < 0) continue;
        if (itemType_[static_cast<std::size_t>(item)] != recipe.inputType) continue;
        if (itemProgress_[static_cast<std::size_t>(item)] < 0.85f) continue;
        if (static_cast<Dir>(itemDir_[static_cast<std::size_t>(item)]) != Dir::East) continue;
        destroyItemAt(static_cast<std::size_t>(item));
        ++machine.inputStored;
    }
}

void Logistics::tryPushOutputs() {
    for (Machine& machine : machines_) {
        if (!machine.alive || machine.outputStored <= 0 || machine.recipeIndex < 0) continue;
        const Recipe& recipe = recipes_[static_cast<std::size_t>(machine.recipeIndex)];
        const int     px     = machine.originX + machine.width;
        const int     py     = machine.originY;
        if (!inBounds(px, py, machine.layer)) continue;
        if (cells_[cellIndex(px, py, machine.layer)].kind != CellKind::Belt) continue;
        if (findItemAt(px, py, machine.layer) >= 0) continue;
        auto spawned = spawnItem(px, py, recipe.outputType, machine.layer);
        if (!spawned.ok()) {
            spawned.ignore("machine output blocked");
            continue;
        }
        --machine.outputStored;
    }
}

void Logistics::stepMachines(float dt, StepReport& report) {
    tryPullInputs();
    for (Machine& machine : machines_) {
        if (!machine.alive || machine.recipeIndex < 0) continue;
        const Recipe& recipe = recipes_[static_cast<std::size_t>(machine.recipeIndex)];
        if (!machine.crafting) {
            if (machine.inputStored >= recipe.inputCount &&
                machine.outputStored + recipe.outputCount <= bufferCapacity_) {
                machine.inputStored -= recipe.inputCount;
                machine.crafting      = true;
                machine.craftProgress = 0.f;
                ++report.craftsStarted;
            }
        }
        if (!machine.crafting) continue;
        machine.craftProgress += dt / recipe.durationSec;
        if (machine.craftProgress < 1.f) continue;
        machine.crafting      = false;
        machine.craftProgress = 0.f;
        machine.outputStored += recipe.outputCount;
        ++craftsTotal_;
        ++report.craftsFinished;
    }
    tryPushOutputs();
}

void Logistics::stepBelts(float dt, StepReport& report) {
    const float delta = beltSpeed_ * dt;
    // Move from downstream to upstream by scanning opposite to facing so hand-offs are stable.
    std::vector<int> order(static_cast<std::size_t>(itemCount()));
    for (int i = 0; i < itemCount(); ++i) order[static_cast<std::size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int ax = itemCellX_[static_cast<std::size_t>(a)];
        const int ay = itemCellY_[static_cast<std::size_t>(a)];
        const int bx = itemCellX_[static_cast<std::size_t>(b)];
        const int by = itemCellY_[static_cast<std::size_t>(b)];
        const Dir da = static_cast<Dir>(itemDir_[static_cast<std::size_t>(a)]);
        if (da == Dir::East) return ax > bx || (ax == bx && ay > by);
        if (da == Dir::West) return ax < bx || (ax == bx && ay > by);
        if (da == Dir::South) return ay > by || (ay == by && ax > bx);
        return ay < by || (ay == by && ax > bx);
    });

    for (int idx : order) {
        if (idx < 0 || idx >= itemCount()) continue;
        float progress = itemProgress_[static_cast<std::size_t>(idx)] + delta;
        int   cx       = itemCellX_[static_cast<std::size_t>(idx)];
        int   cy       = itemCellY_[static_cast<std::size_t>(idx)];
        int   layer    = itemLayer_[static_cast<std::size_t>(idx)];
        Dir   dir      = static_cast<Dir>(itemDir_[static_cast<std::size_t>(idx)]);
        bool  moved    = false;
        while (progress >= 1.f) {
            int dx = 0, dy = 0;
            dirDelta(dir, dx, dy);
            const int nx = cx + dx;
            const int ny = cy + dy;
            if (!inBounds(nx, ny, layer) || cells_[cellIndex(nx, ny, layer)].kind != CellKind::Belt ||
                findItemAt(nx, ny, layer) >= 0) {
                progress = 0.999f;
                ++report.jammedMoves;
                break;
            }
            occupancy_[cellIndex(cx, cy, layer)] = -1;
            cx                                   = nx;
            cy                                   = ny;
            dir                                  = cells_[cellIndex(nx, ny, layer)].dir;
            occupancy_[cellIndex(cx, cy, layer)] = idx;
            progress -= 1.f;
            moved = true;
        }
        itemCellX_[static_cast<std::size_t>(idx)]    = cx;
        itemCellY_[static_cast<std::size_t>(idx)]    = cy;
        itemLayer_[static_cast<std::size_t>(idx)]    = layer;
        itemDir_[static_cast<std::size_t>(idx)]      = static_cast<std::uint8_t>(dir);
        itemProgress_[static_cast<std::size_t>(idx)] = progress;
        if (moved) ++report.itemsMoved;
    }
}

Result<StepReport> Logistics::advance(const SimulationStep& step) {
    if (auto ready = ensureConfigured(); !ready.ok()) return Result<StepReport>::failure(ready.status());
    if (!(step.delta.seconds() > 0.0) || !std::isfinite(step.delta.seconds()))
        return failT<StepReport>(DiagnosticCode::InvalidArgument, "advance dt must be positive finite", "dt");
    if (step.tick.value() <= tick_.value())
        return failT<StepReport>(DiagnosticCode::Conflict, "simulation tick must strictly increase", "tick");
    StepReport report;
    report.tick    = step.tick;
    report.elapsed = step.delta;
    const float dt = static_cast<float>(step.delta.seconds());
    stepSources(dt, report);
    stepBelts(dt, report);
    stepMachines(dt, report);
    stepSinks(report);
    tick_ = step.tick;
    return Result<StepReport>::success(std::move(report));
}

Result<StepReport> Logistics::advance(float dt) {
    if (!(dt > 0.f) || !std::isfinite(dt))
        return failT<StepReport>(DiagnosticCode::InvalidArgument, "advance dt must be positive finite", "dt");
    auto duration = Duration::fromSeconds(static_cast<double>(dt));
    if (!duration.ok()) return Result<StepReport>::failure(duration.status());
    const SimulationTick next(tick_.value() + 1);
    return advance(SimulationStep{next, std::move(duration).takeValue()});
}

}  // namespace eve::logistics
