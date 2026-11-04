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

Result<void> parseNeedsArray(const Value::Object& object, std::vector<NeedMeterDef>& out) {
    const Value* value = objectField(object, "needs");
    if (!value) return Result<void>::success();
    const auto* array = value->getIf<Value::Array>();
    if (!array) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "needs must be an array", "growth.species.needs"));
    }
    for (const Value& entry : *array) {
        const auto* needObject = entry.getIf<Value::Object>();
        if (!needObject) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "need entry must be an object", "growth.species.needs"));
        }
        NeedMeterDef need;
        if (auto id = readString(*needObject, "id", need.id, true); !id) return id;
        if (need.id.empty()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "need id is empty", "growth.species.needs.id"));
        }
        if (auto d = readDouble(*needObject, "drainPerDay", need.drainPerDay, false); !d) return d;
        if (auto m = readDouble(*needObject, "minPermille", need.minPermille, false); !m) return m;
        if (auto r = readBool(*needObject, "requiredForGrowth", need.requiredForGrowth); !r) return r;
        if (auto l = readBool(*needObject, "lethalWhenEmpty", need.lethalWhenEmpty); !l) return l;
        out.push_back(std::move(need));
    }
    return Result<void>::success();
}

Result<void> parseReproduction(const Value::Object& object, ReproductionDef& out, bool& sawReproductionObject,
                               bool& legacyCanBreed, double& legacyGestation, std::string& legacyOffspring) {
    sawReproductionObject = false;
    if (auto v = readBool(object, "canBreed", legacyCanBreed); !v) return v;
    if (auto v = readDouble(object, "gestationDays", legacyGestation, false); !v) return v;
    if (auto v = readString(object, "offspringSpeciesId", legacyOffspring, false); !v) return v;

    const Value* value = objectField(object, "reproduction");
    if (!value) return Result<void>::success();
    const auto* reproObject = value->getIf<Value::Object>();
    if (!reproObject) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "reproduction must be an object", "growth.species.reproduction"));
    }
    sawReproductionObject = true;
    std::string modeText  = "none";
    if (auto mode = readString(*reproObject, "mode", modeText, false); !mode) return mode;
    auto parsedMode = parseReproductionMode(modeText);
    if (!parsedMode) return Result<void>::failure(parsedMode.status());
    out.mode = parsedMode.value();
    if (auto v = readDouble(*reproObject, "gestationDays", out.gestationDays, false); !v) return v;
    if (auto v = readDouble(*reproObject, "intervalDays", out.intervalDays, false); !v) return v;
    if (auto v = readInt(*reproObject, "offspringCountMin", out.offspringCountMin, false); !v) return v;
    out.offspringCountMax = out.offspringCountMin;
    if (auto v = readInt(*reproObject, "offspringCountMax", out.offspringCountMax, false); !v) return v;
    if (auto v = readString(*reproObject, "offspringSpeciesId", out.offspringSpeciesId, false); !v) return v;
    if (auto v = readBool(*reproObject, "requiresPartner", out.requiresPartner); !v) return v;
    if (auto v = readBool(*reproObject, "partnerMustSameSpecies", out.partnerMustSameSpecies); !v) return v;
    if (auto v = readBool(*reproObject, "parentDiesOnBirth", out.parentDiesOnBirth); !v) return v;
    if (auto v = readBool(*reproObject, "parentResetsStagesOnBirth", out.parentResetsStagesOnBirth); !v) return v;
    if (out.offspringCountMin < 0 || out.offspringCountMax < out.offspringCountMin) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "offspring counts must satisfy 0 <= min <= max",
                                                       "growth.species.reproduction"));
    }
    return Result<void>::success();
}

Result<SpeciesDef> parseSpeciesObject(const Value::Object& object) {
    SpeciesDef def;
    if (auto id = readString(object, "id", def.id, true); !id) return fail<SpeciesDef>(id.status());
    if (auto name = readString(object, "displayName", def.displayName, false); !name)
        return fail<SpeciesDef>(name.status());
    if (def.displayName.empty()) def.displayName = def.id;

    std::string kindText = "organism";
    if (auto kind = readString(object, "kind", kindText, false); !kind) return fail<SpeciesDef>(kind.status());
    def.kindId          = kindText;
    auto parsedKind     = parseOrganismKind(kindText);
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

    if (auto needs = parseNeedsArray(object, def.needs); !needs) return fail<SpeciesDef>(needs.status());

    // Legacy Earth-farm fields — folded into needs by normalizeSpeciesDef when needs is empty.
    double waterDrain = 0.0, foodDrain = 0.0, fertilityDrain = 0.0;
    double minWater = 200.0, minFood = 200.0, minFertility = 100.0;
    bool   sawWater = objectField(object, "waterDrainPerDay") != nullptr;
    bool   sawFood  = objectField(object, "foodDrainPerDay") != nullptr;
    bool   sawFert  = objectField(object, "fertilityDrainPerDay") != nullptr;
    if (auto v = readDouble(object, "waterDrainPerDay", waterDrain, false); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "foodDrainPerDay", foodDrain, false); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "fertilityDrainPerDay", fertilityDrain, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minWaterPermille", minWater, false); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minFoodPermille", minFood, false); !v) return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "minFertilityPermille", minFertility, false); !v)
        return fail<SpeciesDef>(v.status());
    if (def.needs.empty()) {
        if (def.kind == OrganismKind::Animal || sawFood || sawWater) {
            if (def.kind == OrganismKind::Animal || sawWater || !sawFert) {
                def.needs.push_back(NeedMeterDef{"water", waterDrain > 0.0 ? waterDrain : 50.0, minWater, true, true});
            }
            if (def.kind == OrganismKind::Animal || sawFood) {
                def.needs.push_back(NeedMeterDef{"food", foodDrain > 0.0 ? foodDrain : 50.0, minFood, true, true});
            }
        }
        if (def.kind == OrganismKind::Plant || sawFert) {
            if (def.needs.empty() || def.kind == OrganismKind::Plant) {
                if (!def.hasNeed("water")) {
                    def.needs.push_back(
                        NeedMeterDef{"water", waterDrain > 0.0 ? waterDrain : 50.0, minWater, true, true});
                }
                def.needs.push_back(
                    NeedMeterDef{"fertility", fertilityDrain > 0.0 ? fertilityDrain : 20.0, minFertility, true, true});
            }
        }
        if (def.needs.empty() && (sawWater || sawFood || sawFert)) {
            if (sawWater)
                def.needs.push_back(NeedMeterDef{"water", waterDrain, minWater, true, true});
            if (sawFood) def.needs.push_back(NeedMeterDef{"food", foodDrain, minFood, true, true});
            if (sawFert) def.needs.push_back(NeedMeterDef{"fertility", fertilityDrain, minFertility, true, true});
        }
    }

    if (auto v = readDouble(object, "produceIntervalDays", def.produceIntervalDays, false); !v)
        return fail<SpeciesDef>(v.status());
    if (auto v = readDouble(object, "baseDiseaseResist", def.baseDiseaseResist, false); !v)
        return fail<SpeciesDef>(v.status());

    bool        sawReproduction = false;
    bool        legacyCanBreed  = false;
    double      legacyGestation = 0.0;
    std::string legacyOffspring;
    if (auto repro = parseReproduction(object, def.reproduction, sawReproduction, legacyCanBreed, legacyGestation,
                                       legacyOffspring);
        !repro)
        return fail<SpeciesDef>(repro.status());
    if (!sawReproduction) {
        if (legacyCanBreed) {
            def.reproduction.mode              = ReproductionMode::PairGestation;
            def.reproduction.gestationDays     = legacyGestation;
            def.reproduction.requiresPartner   = true;
            def.reproduction.offspringSpeciesId =
                legacyOffspring.empty() ? def.id : legacyOffspring;
        } else if (!legacyOffspring.empty()) {
            def.reproduction.offspringSpeciesId = legacyOffspring;
        }
    } else if (def.reproduction.offspringSpeciesId.empty() && !legacyOffspring.empty()) {
        def.reproduction.offspringSpeciesId = legacyOffspring;
    }

    if (const Value* season = objectField(object, "seasonMul")) {
        const auto* array = season->getIf<Value::Array>();
        if (!array || array->empty()) {
            return Result<SpeciesDef>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "seasonMul must be a non-empty number array",
                                                                 "growth.species.seasonMul"));
        }
        def.seasonMul.clear();
        def.seasonMul.reserve(array->size());
        for (const Value& entry : *array) {
            if (const auto* number = entry.getIf<double>()) {
                def.seasonMul.push_back(*number);
            } else if (const auto* integer = entry.getIf<std::int64_t>()) {
                def.seasonMul.push_back(static_cast<double>(*integer));
            } else {
                return Result<SpeciesDef>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "seasonMul entries must be numbers", "growth.species.seasonMul"));
            }
        }
    }

    if (const Value* byId = objectField(object, "seasonMulById")) {
        const auto* mapObject = byId->getIf<Value::Object>();
        if (!mapObject) {
            return Result<SpeciesDef>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "seasonMulById must be an object", "growth.species.seasonMulById"));
        }
        for (const auto& [seasonId, mulValue] : *mapObject) {
            if (const auto* number = mulValue.getIf<double>()) {
                def.seasonMulById[seasonId] = *number;
            } else if (const auto* integer = mulValue.getIf<std::int64_t>()) {
                def.seasonMulById[seasonId] = static_cast<double>(*integer);
            } else {
                return Result<SpeciesDef>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "seasonMulById values must be numbers",
                                                                     "growth.species.seasonMulById"));
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

    if (auto ok = normalizeSpeciesDef(def); !ok) return fail<SpeciesDef>(ok.status());
    return Result<SpeciesDef>::success(std::move(def));
}

}  // namespace

bool SpeciesDef::hasNeed(std::string_view needId) const { return findNeed(needId) != nullptr; }

const NeedMeterDef* SpeciesDef::findNeed(std::string_view needId) const {
    for (const NeedMeterDef& need : needs) {
        if (need.id == needId) return &need;
    }
    return nullptr;
}

Result<OrganismKind> parseOrganismKind(std::string_view text) {
    const std::string lower = lowerCopy(text);
    if (lower == "plant") return Result<OrganismKind>::success(OrganismKind::Plant);
    if (lower == "animal") return Result<OrganismKind>::success(OrganismKind::Animal);
    if (lower == "custom" || lower == "organism" || lower.empty())
        return Result<OrganismKind>::success(OrganismKind::Custom);
    // Any other tag is a valid custom lifeform id (xeno.crystal, slime, …).
    return Result<OrganismKind>::success(OrganismKind::Custom);
}

Result<ReproductionMode> parseReproductionMode(std::string_view text) {
    const std::string lower = lowerCopy(text);
    if (lower == "none" || lower.empty()) return Result<ReproductionMode>::success(ReproductionMode::None);
    if (lower == "pair_gestation" || lower == "pairgestation" || lower == "gestation")
        return Result<ReproductionMode>::success(ReproductionMode::PairGestation);
    if (lower == "self_bud" || lower == "selfbud" || lower == "bud")
        return Result<ReproductionMode>::success(ReproductionMode::SelfBud);
    if (lower == "fission" || lower == "split")
        return Result<ReproductionMode>::success(ReproductionMode::Fission);
    if (lower == "spore_burst" || lower == "sporeburst" || lower == "spore")
        return Result<ReproductionMode>::success(ReproductionMode::SporeBurst);
    return Result<ReproductionMode>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument,
        "reproduction.mode must be none|pair_gestation|self_bud|fission|spore_burst",
        "growth.species.reproduction.mode"));
}

Result<void> normalizeSpeciesDef(SpeciesDef& def) {
    if (def.id.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "species id is empty", "growth.species.id"));
    }
    if (def.kindId.empty()) def.kindId = "organism";
    const std::string lowerKind = lowerCopy(def.kindId);
    if (lowerKind == "plant")
        def.kind = OrganismKind::Plant;
    else if (lowerKind == "animal")
        def.kind = OrganismKind::Animal;
    else
        def.kind = OrganismKind::Custom;

    if (def.reproduction.offspringSpeciesId.empty()) def.reproduction.offspringSpeciesId = def.id;
    if (def.reproduction.mode == ReproductionMode::PairGestation) {
        def.reproduction.requiresPartner = true;
        if (!(def.reproduction.gestationDays > 0.0)) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "pair_gestation requires gestationDays > 0",
                                                           "growth.species.reproduction.gestationDays"));
        }
    }
    if (def.reproduction.mode == ReproductionMode::SelfBud || def.reproduction.mode == ReproductionMode::Fission ||
        def.reproduction.mode == ReproductionMode::SporeBurst) {
        if (!(def.reproduction.intervalDays > 0.0)) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "interval reproduction requires intervalDays > 0",
                                                           "growth.species.reproduction.intervalDays"));
        }
    }
    if (def.seasonMul.empty()) def.seasonMul = {1.0, 1.0, 1.0, 1.0};

    // Deduplicate need ids.
    std::unordered_map<std::string, int> seen;
    for (const NeedMeterDef& need : def.needs) {
        if (need.id.empty()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "need id is empty", "growth.species.needs"));
        }
        if (++seen[need.id] > 1) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "duplicate need id: " + need.id, "growth.species.needs"));
        }
    }
    return Result<void>::success();
}

std::unordered_map<std::string, SpeciesDef>& SpeciesRegistry::table() {
    static std::unordered_map<std::string, SpeciesDef> instance;
    return instance;
}

Result<void> SpeciesRegistry::registerSpecies(const SpeciesDef& def) {
    SpeciesDef copy = def;
    if (auto ok = normalizeSpeciesDef(copy); !ok) return ok;
    if (copy.stages.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "species requires at least one stage", "growth.species.stages"));
    }
    for (const GrowthStageDef& stage : copy.stages) {
        if (stage.id.empty() || !(stage.durationDays > 0.0) || !std::isfinite(stage.durationDays)) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid growth stage", "growth.species.stages"));
        }
    }
    // C++ authors may still fill only legacy-shaped fields via helpers; synthesize needs if empty.
    if (copy.needs.empty()) {
        if (copy.kind == OrganismKind::Animal) {
            copy.needs.push_back(NeedMeterDef{"water", 50.0, 100.0, true, true});
            copy.needs.push_back(NeedMeterDef{"food", 50.0, 100.0, true, true});
        } else if (copy.kind == OrganismKind::Plant) {
            copy.needs.push_back(NeedMeterDef{"water", 50.0, 100.0, true, true});
            copy.needs.push_back(NeedMeterDef{"fertility", 20.0, 50.0, true, true});
        }
    }
    table()[copy.id] = std::move(copy);
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
