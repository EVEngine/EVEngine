#include "warship/WarshipRegistry.h"

#include "common/Diagnostic.h"
#include "common/Value.h"
#include "common/ValueRead.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve::warship {
namespace {

template <typename T>
Result<T> fail(const Status& status) {
    return Result<T>::failure(status);
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

Result<std::string> optionalString(const Value::Object& object, std::string_view name, std::string fallback) {
    if (!field(object, name)) return Result<std::string>::success(std::move(fallback));
    return readStringMember(object, name);
}

Result<float> optionalFloat(const Value::Object& object, std::string_view name, float fallback) {
    if (!field(object, name)) return Result<float>::success(fallback);
    auto parsed = readFiniteFloatValue(*field(object, name));
    if (!parsed) return fail<float>(parsed.status());
    return Result<float>::success(parsed.value());
}

Result<int> optionalInt(const Value::Object& object, std::string_view name, int fallback) {
    if (!field(object, name)) return Result<int>::success(fallback);
    auto parsed = readNumericDoubleValue(*field(object, name));
    if (!parsed) return fail<int>(parsed.status());
    const double number = parsed.value();
    if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "integer field out of range", "warship.registry"));
    }
    return Result<int>::success(static_cast<int>(number));
}

Result<std::vector<std::string>> optionalStringArray(const Value::Object& object, std::string_view name) {
    std::vector<std::string> out;
    const Value*             value = field(object, name);
    if (!value) return Result<std::vector<std::string>>::success(std::move(out));
    const auto* array = value->getIf<Value::Array>();
    if (!array) {
        return Result<std::vector<std::string>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "tags must be a string array", "warship.registry"));
    }
    for (const Value& entry : *array) {
        const auto* text = entry.getIf<std::string>();
        if (!text) {
            return Result<std::vector<std::string>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "tag entries must be strings", "warship.registry"));
        }
        out.push_back(*text);
    }
    return Result<std::vector<std::string>>::success(std::move(out));
}

Result<CompartmentFunctionDef> parseFunction(const Value::Object& object) {
    CompartmentFunctionDef def;
    auto                   id = readStringMember(object, "id");
    if (!id) return fail<CompartmentFunctionDef>(id.status());
    if (id.value().empty()) {
        return Result<CompartmentFunctionDef>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "function id must be non-empty", "warship.function"));
    }
    def.id = std::move(id).takeValue();

    auto display = optionalString(object, "name", def.id);
    if (!display) return fail<CompartmentFunctionDef>(display.status());
    def.displayName = std::move(display).takeValue();

    auto powerDraw = optionalFloat(object, "powerDraw", 0.f);
    if (!powerDraw) return fail<CompartmentFunctionDef>(powerDraw.status());
    def.powerDraw = powerDraw.value();

    auto powerProvide = optionalFloat(object, "powerProvide", 0.f);
    if (!powerProvide) return fail<CompartmentFunctionDef>(powerProvide.status());
    def.powerProvide = powerProvide.value();

    auto thrustBonus = optionalFloat(object, "thrustBonus", 0.f);
    if (!thrustBonus) return fail<CompartmentFunctionDef>(thrustBonus.status());
    def.thrustBonus = thrustBonus.value();

    auto hangar = optionalInt(object, "hangarCapacity", 0);
    if (!hangar) return fail<CompartmentFunctionDef>(hangar.status());
    def.hangarCapacity = hangar.value();

    auto weapons = optionalInt(object, "weaponBayCount", 0);
    if (!weapons) return fail<CompartmentFunctionDef>(weapons.status());
    def.weaponBayCount = weapons.value();

    auto crew = optionalInt(object, "crewCapacity", 0);
    if (!crew) return fail<CompartmentFunctionDef>(crew.status());
    def.crewCapacity = crew.value();

    auto sail = optionalFloat(object, "sailPower", 0.f);
    if (!sail) return fail<CompartmentFunctionDef>(sail.status());
    def.sailPower = sail.value();

    auto tags = optionalStringArray(object, "tags");
    if (!tags) return fail<CompartmentFunctionDef>(tags.status());
    def.tags = std::move(tags).takeValue();
    return Result<CompartmentFunctionDef>::success(std::move(def));
}

Result<LayoutSeed> parseSeed(const Value::Object& object) {
    LayoutSeed seed;
    auto       deck = optionalInt(object, "deck", 0);
    if (!deck) return fail<LayoutSeed>(deck.status());
    seed.deck = deck.value();
    auto row  = optionalInt(object, "row", 0);
    if (!row) return fail<LayoutSeed>(row.status());
    seed.row = row.value();
    auto col = optionalInt(object, "col", 0);
    if (!col) return fail<LayoutSeed>(col.status());
    seed.col        = col.value();
    auto functionId = readStringMember(object, "function");
    if (!functionId) {
        functionId = readStringMember(object, "functionId");
    }
    if (!functionId) return fail<LayoutSeed>(functionId.status());
    seed.functionId = std::move(functionId).takeValue();
    if (seed.functionId.empty()) {
        return Result<LayoutSeed>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                             "layout function id must be non-empty", "warship.layout"));
    }
    return Result<LayoutSeed>::success(std::move(seed));
}

Result<WarshipDefinition> parseHull(const Value::Object& object) {
    WarshipDefinition def;
    auto              id = readStringMember(object, "id");
    if (!id) return fail<WarshipDefinition>(id.status());
    if (id.value().empty()) {
        return Result<WarshipDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "hull id must be non-empty", "warship.hull"));
    }
    def.id = std::move(id).takeValue();

    auto category = optionalString(object, "category", kCategoryWarship);
    if (!category) return fail<WarshipDefinition>(category.status());
    def.category = std::move(category).takeValue();

    auto mobility = optionalString(object, "mobility", kMobilitySurface);
    if (!mobility) return fail<WarshipDefinition>(mobility.status());
    def.mobility = std::move(mobility).takeValue();

    auto length = optionalFloat(object, "length", def.length);
    if (!length) return fail<WarshipDefinition>(length.status());
    def.length = length.value();
    auto beam  = optionalFloat(object, "beam", def.beam);
    if (!beam) return fail<WarshipDefinition>(beam.status());
    def.beam      = beam.value();
    auto maxSpeed = optionalFloat(object, "maxSpeed", def.maxSpeed);
    if (!maxSpeed) return fail<WarshipDefinition>(maxSpeed.status());
    def.maxSpeed = maxSpeed.value();
    auto accel   = optionalFloat(object, "accel", def.accel);
    if (!accel) return fail<WarshipDefinition>(accel.status());
    def.accel     = accel.value();
    auto turnRate = optionalFloat(object, "turnRate", def.turnRate);
    if (!turnRate) return fail<WarshipDefinition>(turnRate.status());
    def.turnRate   = turnRate.value();
    auto maxHealth = optionalFloat(object, "maxHealth", def.maxHealth);
    if (!maxHealth) return fail<WarshipDefinition>(maxHealth.status());
    def.maxHealth = maxHealth.value();

    if (def.length <= 0.f || def.beam <= 0.f || def.maxSpeed < 0.f || def.accel < 0.f || def.turnRate < 0.f ||
        def.maxHealth < 0.f) {
        return Result<WarshipDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "hull numeric fields must be non-negative (size > 0)", "warship.hull"));
    }

    if (const Value* gridValue = field(object, "grid")) {
        const auto* gridObject = gridValue->getIf<Value::Object>();
        if (!gridObject) {
            return Result<WarshipDefinition>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "grid must be an object", "warship.hull"));
        }
        auto cols = optionalInt(*gridObject, "cols", 4);
        if (!cols) return fail<WarshipDefinition>(cols.status());
        auto rows = optionalInt(*gridObject, "rows", 2);
        if (!rows) return fail<WarshipDefinition>(rows.status());
        auto decks = optionalInt(*gridObject, "decks", 1);
        if (!decks) return fail<WarshipDefinition>(decks.status());
        if (cols.value() <= 0 || rows.value() <= 0 || decks.value() <= 0) {
            return Result<WarshipDefinition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "grid cols/rows/decks must be positive", "warship.hull"));
        }
        def.grid.cols  = cols.value();
        def.grid.rows  = rows.value();
        def.grid.decks = decks.value();
    } else {
        def.grid.cols  = 4;
        def.grid.rows  = 2;
        def.grid.decks = 1;
    }

    if (const Value* layoutValue = field(object, "defaultLayout")) {
        const auto* array = layoutValue->getIf<Value::Array>();
        if (!array) {
            return Result<WarshipDefinition>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "defaultLayout must be an array", "warship.hull"));
        }
        for (const Value& entry : *array) {
            const auto* seedObject = entry.getIf<Value::Object>();
            if (!seedObject) {
                return Result<WarshipDefinition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "defaultLayout entries must be objects", "warship.hull"));
            }
            auto seed = parseSeed(*seedObject);
            if (!seed) return fail<WarshipDefinition>(seed.status());
            if (def.grid.indexOf(seed.value().deck, seed.value().row, seed.value().col) < 0) {
                return Result<WarshipDefinition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "defaultLayout cell is outside the hull grid", "warship.hull"));
            }
            def.defaultLayout.push_back(std::move(seed).takeValue());
        }
    }

    auto tags = optionalStringArray(object, "tags");
    if (!tags) return fail<WarshipDefinition>(tags.status());
    def.tags = std::move(tags).takeValue();
    return Result<WarshipDefinition>::success(std::move(def));
}

}  // namespace

std::unordered_map<std::string, WarshipDefinition>& WarshipRegistry::hullTable() {
    static std::unordered_map<std::string, WarshipDefinition> table;
    return table;
}

std::unordered_map<std::string, CompartmentFunctionDef>& WarshipRegistry::functionTable() {
    static std::unordered_map<std::string, CompartmentFunctionDef> table;
    return table;
}

Result<int> WarshipRegistry::loadHullsFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return fail<int>(parsed.status());
    Value root = std::move(parsed).takeValue();

    int  registered = 0;
    auto accept     = [&](const Value::Object& object) -> Result<void> {
        auto def = parseHull(object);
        if (!def) return Result<void>::failure(def.status());
        WarshipDefinition parsed = std::move(def).takeValue();
        hullTable()[parsed.id]   = std::move(parsed);
        ++registered;
        return Result<void>::success();
    };

    if (const auto* array = root.getIf<Value::Array>()) {
        for (const Value& entry : *array) {
            const auto* object = entry.getIf<Value::Object>();
            if (!object) {
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "hull array entries must be objects", "warship.hull"));
            }
            if (auto ok = accept(*object); !ok) return fail<int>(ok.status());
        }
        return Result<int>::success(registered);
    }
    const auto* object = root.getIf<Value::Object>();
    if (!object) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "hull JSON must be an object or array", "warship.hull"));
    }
    if (auto ok = accept(*object); !ok) return fail<int>(ok.status());
    return Result<int>::success(registered);
}

Result<int> WarshipRegistry::loadFunctionsFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return fail<int>(parsed.status());
    Value root = std::move(parsed).takeValue();

    int  registered = 0;
    auto accept     = [&](const Value::Object& object) -> Result<void> {
        auto def = parseFunction(object);
        if (!def) return Result<void>::failure(def.status());
        CompartmentFunctionDef parsed = std::move(def).takeValue();
        functionTable()[parsed.id]    = std::move(parsed);
        ++registered;
        return Result<void>::success();
    };

    if (const auto* array = root.getIf<Value::Array>()) {
        for (const Value& entry : *array) {
            const auto* object = entry.getIf<Value::Object>();
            if (!object) {
                return Result<int>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "function array entries must be objects", "warship.function"));
            }
            if (auto ok = accept(*object); !ok) return fail<int>(ok.status());
        }
        return Result<int>::success(registered);
    }
    const auto* object = root.getIf<Value::Object>();
    if (!object) {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "function JSON must be an object or array", "warship.function"));
    }
    if (auto ok = accept(*object); !ok) return fail<int>(ok.status());
    return Result<int>::success(registered);
}

void WarshipRegistry::clearHulls() { hullTable().clear(); }

void WarshipRegistry::clearFunctions() { functionTable().clear(); }

bool WarshipRegistry::hasHull(const std::string& id) { return findHull(id) != nullptr; }

bool WarshipRegistry::hasFunction(const std::string& id) { return findFunction(id) != nullptr; }

int WarshipRegistry::hullCount() { return static_cast<int>(hullTable().size()); }

int WarshipRegistry::functionCount() { return static_cast<int>(functionTable().size()); }

const WarshipDefinition* WarshipRegistry::findHull(const std::string& id) {
    auto it = hullTable().find(id);
    if (it == hullTable().end()) return nullptr;
    return &it->second;
}

const CompartmentFunctionDef* WarshipRegistry::findFunction(const std::string& id) {
    auto it = functionTable().find(id);
    if (it == functionTable().end()) return nullptr;
    return &it->second;
}

}  // namespace eve::warship
