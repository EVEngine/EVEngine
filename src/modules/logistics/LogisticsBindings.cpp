#include "logistics/Logistics.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <utility>

namespace eve::logistics {

void Logistics::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Logistics::create, false);
    expose(cls);

    auto item = table.addClass<ItemState>("LogisticsItemState", ssq::Class::Ctor<ItemState()>());
    item.addVar("x", &ItemState::x);
    item.addVar("y", &ItemState::y);
    item.addVar("type", &ItemState::type);
    item.addVar("cellX", &ItemState::cellX);
    item.addVar("cellY", &ItemState::cellY);
    item.addVar("progress", &ItemState::progress);
    item.addVar("dir", &ItemState::dir);

    auto machine = table.addClass<MachineState>("LogisticsMachineState", ssq::Class::Ctor<MachineState()>());
    machine.addVar("id", &MachineState::id);
    machine.addVar("originX", &MachineState::originX);
    machine.addVar("originY", &MachineState::originY);
    machine.addVar("width", &MachineState::width);
    machine.addVar("height", &MachineState::height);
    machine.addVar("recipeIndex", &MachineState::recipeIndex);
    machine.addVar("inputStored", &MachineState::inputStored);
    machine.addVar("outputStored", &MachineState::outputStored);
    machine.addVar("craftProgress", &MachineState::craftProgress);
    machine.addVar("crafting", &MachineState::crafting);
    machine.addVar("layer", &MachineState::layer);
}

void Logistics::expose(ssq::Class& cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("getName", &Logistics::getName);
    cls.addFunc("configure", [vm](Logistics* self, int width, int height, int layers, float cellSize, float originX,
                                  float originY) {
        return script::projectResult(vm, self->configure(width, height, layers, cellSize, originX, originY));
    });
    cls.addFunc("clear", &Logistics::clear);
    cls.addFunc("registerRecipe", [vm](Logistics* self, ssq::Table recipe) {
        try {
            const std::string id          = recipe.get<std::string>("id");
            const int         inputType   = recipe.get<int>("inputType");
            const int         inputCount  = recipe.get<int>("inputCount");
            const int         outputType  = recipe.get<int>("outputType");
            const int         outputCount = recipe.get<int>("outputCount");
            const float       durationSec = recipe.get<float>("durationSec");
            return script::projectResult(
                vm, self->registerRecipe(id, inputType, inputCount, outputType, outputCount, durationSec),
                [](int index) { return Value(index); });
        } catch (const std::exception& error) {
            return script::projectResult(
                vm, Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "recipe")),
                [](int index) { return Value(index); });
        }
    });
    cls.addFunc("placeBelt", [vm](Logistics* self, int cx, int cy, int dir, int layer) {
        return script::projectResult(vm, self->placeBelt(cx, cy, dir, layer));
    });
    cls.addFunc("clearCell", [vm](Logistics* self, int cx, int cy, int layer) {
        return script::projectResult(vm, self->clearCell(cx, cy, layer));
    });
    cls.addFunc("placeMachine",
                [vm](Logistics* self, int cx, int cy, int width, int height, const std::string& recipeId, int layer) {
                    return script::projectResult(vm, self->placeMachine(cx, cy, width, height, recipeId, layer),
                                                 [](int id) { return Value(id); });
                });
    cls.addFunc("removeMachine", [vm](Logistics* self, int machineId) {
        return script::projectResult(vm, self->removeMachine(machineId));
    });
    cls.addFunc("placeSource", [vm](Logistics* self, int cx, int cy, int itemType, float itemsPerSec, int layer) {
        return script::projectResult(vm, self->placeSource(cx, cy, itemType, itemsPerSec, layer));
    });
    cls.addFunc("placeSink", [vm](Logistics* self, int cx, int cy, int layer) {
        return script::projectResult(vm, self->placeSink(cx, cy, layer));
    });
    cls.addFunc("spawnItem", [vm](Logistics* self, int cx, int cy, int itemType, int layer) {
        return script::projectResult(vm, self->spawnItem(cx, cy, itemType, layer), [](int id) { return Value(id); });
    });
    cls.addFunc("advance", [vm](Logistics* self, float dt) {
        return script::projectResult(vm, self->advance(dt), [](StepReport report) {
            return Value(Value::Object{{"tick", Value(std::to_string(report.tick.value()))},
                                       {"elapsedNanoseconds", Value(report.elapsed.nanoseconds())},
                                       {"itemsMoved", Value(report.itemsMoved)},
                                       {"craftsStarted", Value(report.craftsStarted)},
                                       {"craftsFinished", Value(report.craftsFinished)},
                                       {"itemsSpawned", Value(report.itemsSpawned)},
                                       {"itemsSunk", Value(report.itemsSunk)},
                                       {"jammedMoves", Value(report.jammedMoves)}});
        });
    });
    cls.addFunc("getWidth", [](Logistics* self) { return self->width(); });
    cls.addFunc("getHeight", [](Logistics* self) { return self->height(); });
    cls.addFunc("getLayers", [](Logistics* self) { return self->layers(); });
    cls.addFunc("getCellSize", [](Logistics* self) { return self->cellSize(); });
    cls.addFunc("getOriginX", [](Logistics* self) { return self->originX(); });
    cls.addFunc("getOriginY", [](Logistics* self) { return self->originY(); });
    cls.addFunc("getCurrentTick",
                [](Logistics* self) { return static_cast<std::int64_t>(self->currentTick().value()); });
    cls.addFunc("cellKind", [](Logistics* self, int cx, int cy, int layer) { return self->cellKind(cx, cy, layer); });
    cls.addFunc("cellDir", [](Logistics* self, int cx, int cy, int layer) { return self->cellDir(cx, cy, layer); });
    cls.addFunc("cellMachine",
                [](Logistics* self, int cx, int cy, int layer) { return self->cellMachine(cx, cy, layer); });
    cls.addFunc("getItemCount", [](Logistics* self) { return self->itemCount(); });
    cls.addFunc("getItem", [](Logistics* self, int index) {
        auto state = self->itemAt(index);
        if (!state.ok()) {
            state.ignore("getItem out of range");
            return ItemState{};
        }
        return std::move(state).takeValue();
    });
    cls.addFunc("packItemSimFloats", [vm](Logistics* self) {
        return script::projectResult(vm, self->packItemSimFloats(), [](std::vector<float> floats) {
            Value::Array arr;
            arr.reserve(floats.size());
            for (float f : floats) arr.emplace_back(f);
            return Value(std::move(arr));
        });
    });
    cls.addFunc("packItemWorldFloats", [vm](Logistics* self) {
        return script::projectResult(vm, self->packItemWorldFloats(), [](std::vector<float> floats) {
            Value::Array arr;
            arr.reserve(floats.size());
            for (float f : floats) arr.emplace_back(f);
            return Value(std::move(arr));
        });
    });
    cls.addFunc("fillItemWorldFloats", [](Logistics* self, ssq::Array xs, ssq::Array ys, ssq::Array types) {
        const int         n     = self->itemCount();
        const std::size_t count = std::min({xs.size(), ys.size(), types.size(), static_cast<std::size_t>(n)});
        for (std::size_t i = 0; i < count; ++i) {
            auto state = self->itemAt(static_cast<int>(i));
            if (!state.ok()) {
                state.ignore("fillItemWorldFloats skipped missing item");
                continue;
            }
            const ItemState& snap = state.value();
            xs.set(i, snap.x);
            ys.set(i, snap.y);
            types.set(i, static_cast<float>(snap.type));
        }
        return static_cast<int>(count);
    });
    cls.addFunc("getMachineSlotCount", [](Logistics* self) { return self->machineSlotCount(); });
    cls.addFunc("getMachine", [](Logistics* self, int index) {
        auto state = self->machineAt(index);
        if (!state.ok()) {
            state.ignore("getMachine empty slot");
            return MachineState{};
        }
        return std::move(state).takeValue();
    });
    cls.addFunc("machineOwnerId", [](Logistics* self, int index) { return self->machineOwnerId(index); });
    cls.addFunc("getSunkTotal", [](Logistics* self) { return self->sunkTotal(); });
    cls.addFunc("getCraftsTotal", [](Logistics* self) { return self->craftsTotal(); });
    cls.addFunc("setBeltSpeed", [](Logistics* self, float speed) { self->setBeltSpeed(speed); });
    cls.addFunc("getBeltSpeed", [](Logistics* self) { return self->beltSpeed(); });
    cls.addFunc("setBufferCapacity", [](Logistics* self, int capacity) { self->setBufferCapacity(capacity); });
    cls.addFunc("getBufferCapacity", [](Logistics* self) { return self->bufferCapacity(); });
}

}  // namespace eve::logistics
