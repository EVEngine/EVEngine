#include "growth/Species.h"

#include "common/Diagnostic.h"
#include "common/Value.h"

#include <cctype>
#include <cmath>

namespace eve::growth {
namespace {

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const Value* objectField(const Value::Object& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end()) return nullptr;
    return &it->second;
}

template <class T>
Result<T> fail(const Status& status) {
    return Result<T>::failure(status);
}

Result<void> readString(const Value::Object& object, const char* key, std::string& out, bool required) {
    const Value* value = objectField(object, key);
    if (!value) {
        if (required) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           std::string(key) + " is required",
                                                           std::string("growth.species.") + key));
        }
        return Result<void>::success();
    }
    const auto* text = value->getIf<std::string>();
    if (!text) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       std::string(key) + " must be a string",
                                                       std::string("growth.species.") + key));
    }
    out = *text;
    return Result<void>::success();
}

Result<void> readDouble(const Value::Object& object, const char* key, double& out, bool required) {
    const Value* value = objectField(object, key);
    if (!value) {
        if (required) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           std::string(key) + " is required",
                                                           std::string("growth.species.") + key));
        }
        return Result<void>::success();
    }
    if (const auto* number = value->getIf<double>()) {
        out = *number;
        return Result<void>::success();
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        out = static_cast<double>(*integer);
        return Result<void>::success();
    }
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, std::string(key) + " must be a number", std::string("growth.species.") + key));
}

Result<void> readBool(const Value::Object& object, const char* key, bool& out) {
    const Value* value = objectField(object, key);
    if (!value) return Result<void>::success();
    const auto* flag = value->getIf<bool>();
    if (!flag) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       std::string(key) + " must be a bool",
                                                       std::string("growth.species.") + key));
    }
    out = *flag;
    return Result<void>::success();
}

Result<void> readInt(const Value::Object& object, const char* key, int& out, bool required) {
    const Value* value = objectField(object, key);
    if (!value) {
        if (required) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           std::string(key) + " is required",
                                                           std::string("growth.species.") + key));
        }
        return Result<void>::success();
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        out = static_cast<int>(*integer);
        return Result<void>::success();
    }
    if (const auto* number = value->getIf<double>()) {
        out = static_cast<int>(*number);
        return Result<void>::success();
    }
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   std::string(key) + " must be an integer",
                                                   std::string("growth.species.") + key));
}

Result<YieldDef> parseYield(const Value& value) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) {
        return Result<YieldDef>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "yield entry must be an object", "growth.species.yield"));
    }
    YieldDef yield;
    if (auto id = readString(*object, "itemId", yield.itemId, true); !id) return fail<YieldDef>(id.status());
    if (auto min = readInt(*object, "minCount", yield.minCount, false); !min) return fail<YieldDef>(min.status());
    yield.maxCount = yield.minCount;
    if (auto max = readInt(*object, "maxCount", yield.maxCount, false); !max) return fail<YieldDef>(max.status());
    if (yield.minCount < 0 || yield.maxCount < yield.minCount) {
        return Result<YieldDef>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "yield counts must satisfy 0 <= min <= max", "growth.species.yield"));
    }
    return Result<YieldDef>::success(std::move(yield));
}

Result<std::vector<YieldDef>> parseYieldArray(const Value::Object& object, const char* key) {
    std::vector<YieldDef> out;
    const Value*          value = objectField(object, key);
    if (!value) return Result<std::vector<YieldDef>>::success(std::move(out));
    const auto* array = value->getIf<Value::Array>();
    if (!array) {
        return Result<std::vector<YieldDef>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        std::string(key) + " must be an array",
                                                                        std::string("growth.species.") + key));
    }
    for (const Value& entry : *array) {
        auto parsed = parseYield(entry);
        if (!parsed) return fail<std::vector<YieldDef>>(parsed.status());
        out.push_back(std::move(parsed).takeValue());
    }
    return Result<std::vector<YieldDef>>::success(std::move(out));
}

Result<SpeciesDef> parseSpeciesObject(const Value::Object& object) {
    SpeciesDef def;
    if (auto id = readString(object, "id", def.id, true); !id) return fail<SpeciesDef>(id.status());
    if (auto name = readString(object, "displayName", def.displayName, false); !name)
        return fail<SpeciesDef>(name.status());
    if (def.displayName.empty()) def.displayName = def.id;

    std::string kindText = "plant";
    if (auto kind = readString(object, "kind", kindText, false); !kind) return fail<SpeciesDef>(kind.status());
    auto parsedKind = parseOrganismKind(kindText);
    if (!parsedKind) return fail<SpeciesDef>(parsedKind.status());
    def.kind = parsedKind.value();

    const Value* stagesValue = objectField(object, "stages");
    if (!stagesValue) {
        return Result<SpeciesDef>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "stages is required", "growth.species.stages"));
    }
    const auto* stages = stagesValue->getIf<Value::Array>();
    if (!stages || stages->empty()) {
        return Result<SpeciesDef>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "stages must be a non-empty array", "growth.species.stages"));
    }
    for (const Value& stageValue : *stages) {
        const auto* stageObject = stageValue.getIf<Value::Object>();
        if (!stageObject) {
            return Result<SpeciesDef>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "stage must be an object", "growth.species.stages"));
        }
        GrowthStageDef stage;
        if (auto sid = readString(*stageObject, "id", stage.id, true); !sid) return fail<SpeciesDef>(sid.status());
        if (auto dur = readDouble(*stageObject, "durationDays", stage.durationDays, true); !dur)
            return fail<SpeciesDef>(dur.status());
        if (!(stage.durationDays > 0.0) || !std::isfinite(stage.durationDays)) {
            return Result<SpeciesDef>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "durationDays must be finite and > 0",
                                                                 "growth.species.stages.durationDays"));
        }
        def.stages.push_back(std::move(stage));
    }

    if (auto v = readDouble(object, "waterDrainPerDay", def.waterDrainPerDay, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "foodDrainPerDay", def.foodDrainPerDay, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "fertilityDrainPerDay", def.fertilityDrainPerDay, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minWaterPermille", def.minWaterPermille, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minFoodPermille", def.minFoodPermille, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minFertilityPermille", def.minFertilityPermille, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "produceIntervalDays", def.produceIntervalDays, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readBool(object, "canBreed", def.canBreed); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "gestationDays", def.gestationDays, false); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readString(object, "offspringSpeciesId", def.offspringSpeciesId, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "baseDiseaseResist", def.baseDiseaseResist, false); !v)
        return fail<SpeciesDef>(v.status());

    if (const Value* season = objectField(object, "seasonMul")) {
        const auto* array = season->getIf<Value::Array>();
        if (!array || array->size() != 4) {
            return Result<SpeciesDef>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "seasonMul must be an array of 4 numbers",
                                                                 "growth.species.seasonMul"));
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (const auto* number = (*array)[i].getIf<double>()) {
                def.seasonMul[i] = *number;
            } else if (const auto* integer = (*array)[i].getIf<std::int64_t>()) {
                def.seasonMul[i] = static_cast<double>(*integer);
            } else {
                return Result<SpeciesDef>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "seasonMul entries must be numbers", "growth.species.seasonMul"));
            }
        }
    }

    auto harvest = parseYieldArray(object, "harvestYield");
    if (!harvest) return fail<SpeciesDef>(harvest.status());
    def.harvestYield = std::move(harvest).takeValue();

    auto produce = parseYieldArray(object, "produceYield");
    if (!produce) return fail<SpeciesDef>(produce.status());
    def.produceYield = std::move(produce).takeValue();

    if (const Value* diseases = objectField(object, "diseases")) {
        const auto* array = diseases->getIf<Value::Array>();
        if (!array) {
            return Result<SpeciesDef>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "diseases must be an array", "growth.species.diseases"));
        }
        for (const Value& entry : *array) {
            const auto* diseaseObject = entry.getIf<Value::Object>();
            if (!diseaseObject) {
                return Result<SpeciesDef>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "disease entry must be an object", "growth.species.diseases"));
            }
            DiseaseSusceptibility disease;
            if (auto id = readString(*diseaseObject, "diseaseId", disease.diseaseId, true); !id)
                return fail<SpeciesDef>(id.status());
            if (auto c = readDouble(*diseaseObject, "chancePerDay", disease.chancePerDay, false); !c)
                return fail<SpeciesDef>(c.status());
            if (auto d = readDouble(*diseaseObject, "durationDays", disease.durationDays, false); !d)
                return fail<SpeciesDef>(d.status());
            if (auto g = readDouble(*diseaseObject, "growthMul", disease.growthMul, false); !g)
                return fail<SpeciesDef>(g.status());
            def.diseases.push_back(std::move(disease));
        }
    }

    if (def.offspringSpeciesId.empty()) def.offspringSpeciesId = def.id;
    return Result<SpeciesDef>::success(std::move(def));
}

}  // namespace

Result<OrganismKind> parseOrganismKind(std::string_view text) {
    const std::string lower = lowerCopy(text);
    if (lower == "plant") return Result<OrganismKind>::success(OrganismKind::Plant);
    if (lower == "animal") return Result<OrganismKind>::success(OrganismKind::Animal);
    return Result<OrganismKind>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "kind must be plant or animal", "growth.species.kind"));
}

std::unordered_map<std::string, SpeciesDef>& SpeciesRegistry::table() {
    static std::unordered_map<std::string, SpeciesDef> instance;
    return instance;
}

Result<void> SpeciesRegistry::registerSpecies(const SpeciesDef& def) {
    if (def.id.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "species id is empty", "growth.species.id"));
    }
    if (def.stages.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "species requires at least one stage", "growth.species.stages"));
    }
    for (const GrowthStageDef& stage : def.stages) {
        if (stage.id.empty() || !(stage.durationDays > 0.0) || !std::isfinite(stage.durationDays)) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid growth stage", "growth.species.stages"));
        }
    }
    table()[def.id] = def;
    return Result<void>::success();
}

Result<int> SpeciesRegistry::loadFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return fail<int>(parsed.status());
    Value root = std::move(parsed).takeValue();

    int registered = 0;
    if (const auto* array = root.getIf<Value::Array>()) {
        for (const Value& entry : *array) {
            const auto* object = entry.getIf<Value::Object>();
            if (!object) {
                return Result<int>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "species array entries must be objects", "growth.species"));
            }
            auto def = parseSpeciesObject(*object);
            if (!def) return fail<int>(def.status());
            if (auto ok = registerSpecies(def.value()); !ok) return fail<int>(ok.status());
            ++registered;
        }
        return Result<int>::success(registered);
    }

    const auto* object = root.getIf<Value::Object>();
    if (!object) {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "species JSON must be an object or array", "growth.species"));
    }
    auto def = parseSpeciesObject(*object);
    if (!def) return fail<int>(def.status());
    if (auto ok = registerSpecies(def.value()); !ok) return fail<int>(ok.status());
    return Result<int>::success(1);
}

const SpeciesDef* SpeciesRegistry::find(const std::string& id) {
    auto it = table().find(id);
    if (it == table().end()) return nullptr;
    return &it->second;
}

int SpeciesRegistry::count() { return static_cast<int>(table().size()); }

bool SpeciesRegistry::has(const std::string& id) { return find(id) != nullptr; }

void SpeciesRegistry::clear() { table().clear(); }

}  // namespace eve::growth
