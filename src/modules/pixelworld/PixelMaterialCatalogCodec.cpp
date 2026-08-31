#include "pixelworld/PixelMaterialCatalogCodec.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace eve::pixelworld {
namespace {

using Object = Poco::JSON::Object;
using Array = Poco::JSON::Array;

eve::Result<MaterialCatalog> malformed(std::string message, std::string path = "document") {
    return eve::Result<MaterialCatalog>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::SerializationError, std::move(message), std::move(path), {},
        "pixelworld.catalog-codec"));
}

bool exactFields(const Object& object, std::initializer_list<std::string_view> allowed) {
    std::unordered_set<std::string> names;
    for (const auto name : allowed) names.emplace(name);
    if (object.size() != names.size()) return false;
    for (const auto& name : object.getNames())
        if (!names.contains(name)) return false;
    for (const auto& name : names)
        if (!object.has(name)) return false;
    return true;
}

std::string stateName(MaterialState state) {
    switch (state) {
        case MaterialState::Empty: return "empty";
        case MaterialState::Solid: return "solid";
        case MaterialState::Powder: return "powder";
        case MaterialState::Liquid: return "liquid";
        case MaterialState::Gas: return "gas";
        case MaterialState::Energy: return "energy";
    }
    return "empty";
}

bool parseState(const std::string& name, MaterialState& state) {
    if (name == "empty") state = MaterialState::Empty;
    else if (name == "solid") state = MaterialState::Solid;
    else if (name == "powder") state = MaterialState::Powder;
    else if (name == "liquid") state = MaterialState::Liquid;
    else if (name == "gas") state = MaterialState::Gas;
    else if (name == "energy") state = MaterialState::Energy;
    else return false;
    return true;
}

template <class T>
bool readNumber(const Object& object, const std::string& key, T& result) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    if (!object.has(key) || object.isNull(key)) return false;
    try {
        const auto wide = object.get(key).convert<std::int64_t>();
        if constexpr (std::is_unsigned_v<T>) {
            if (wide < 0 || std::uint64_t(wide) > std::numeric_limits<T>::max()) return false;
        } else if (wide < std::numeric_limits<T>::min() ||
                   wide > std::numeric_limits<T>::max()) {
            return false;
        }
        result = static_cast<T>(wide);
        return true;
    } catch (...) {
        return false;
    }
}

Object::Ptr materialJson(const MaterialDefinition& material) {
    Object::Ptr out(new Object);
    out->set("id", std::uint16_t(material.id));
    out->set("name", material.name);
    out->set("state", stateName(material.state));
    out->set("density", material.density);
    out->set("viscosity", material.viscosity);
    out->set("thermalConductivity", material.thermalConductivity);
    out->set("heatCapacity", material.heatCapacity);
    out->set("ignitionTemperature", material.ignitionTemperature);
    out->set("meltingTemperature", material.meltingTemperature);
    out->set("boilingTemperature", material.boilingTemperature);
    out->set("defaultLifetime", material.defaultLifetime);
    out->set("flammable", material.flammable);
    out->set("defaultTemperature", material.defaultTemperature);
    out->set("blastResistance", material.blastResistance);
    out->set("displayRgba", material.displayRgba);
    Array::Ptr tags(new Array);
    for (const auto& tag : material.tags) tags->add(tag);
    out->set("tags", tags);
    return out;
}

}  // namespace

eve::Result<std::string> encodeMaterialCatalogJson(const MaterialCatalog& catalog) {
    Object::Ptr root(new Object);
    root->set("schema", "eve.pixelworld.material-catalog");
    root->set("version", 1);
    Array::Ptr materials(new Array);
    for (const auto& material : catalog.definitions()) materials->add(materialJson(material));
    root->set("materials", materials);
    Array::Ptr reactions(new Array);
    for (const auto& reaction : catalog.reactions()) {
        Object::Ptr value(new Object);
        value->set("id", reaction.id);
        value->set("first", std::uint16_t(reaction.first));
        value->set("second", std::uint16_t(reaction.second));
        value->set("firstResult", std::uint16_t(reaction.firstResult));
        value->set("secondResult", std::uint16_t(reaction.secondResult));
        value->set("minimumTemperature", reaction.minimumTemperature);
        value->set("heatDelta", reaction.heatDelta);
        value->set("priority", reaction.priority);
        reactions->add(value);
    }
    root->set("reactions", reactions);
    Array::Ptr phases(new Array);
    for (const auto& phase : catalog.phaseRules()) {
        Object::Ptr value(new Object);
        value->set("id", phase.id);
        value->set("source", std::uint16_t(phase.source));
        value->set("result", std::uint16_t(phase.result));
        value->set("direction", phase.direction == TemperatureDirection::AtOrBelow
                                    ? "at_or_below"
                                    : "at_or_above");
        value->set("threshold", phase.threshold);
        value->set("temperatureDelta", phase.temperatureDelta);
        value->set("priority", phase.priority);
        phases->add(value);
    }
    root->set("phaseRules", phases);
    std::ostringstream text;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(root), text, 2, -1);
    return eve::Result<std::string>::success(text.str());
}

eve::Result<MaterialCatalog> decodeMaterialCatalogJson(std::string_view json) {
    Object::Ptr root;
    try {
        root = Poco::JSON::Parser().parse(std::string(json)).extract<Object::Ptr>();
    } catch (...) {
        return malformed("material Catalog JSON is malformed");
    }
    if (!root || !exactFields(*root, {"schema", "version", "materials", "reactions", "phaseRules"}))
        return malformed("root contains missing or unknown fields");
    if (root->optValue<std::string>("schema", "") != "eve.pixelworld.material-catalog")
        return malformed("unknown material Catalog schema", "schema");
    int version = 0;
    if (!readNumber(*root, "version", version) || version != 1)
        return malformed("unknown material Catalog version", "version");
    Array::Ptr materials;
    Array::Ptr reactions;
    Array::Ptr phases;
    try {
        materials = root->getArray("materials");
        reactions = root->getArray("reactions");
        phases = root->getArray("phaseRules");
    } catch (...) {
        return malformed("material Catalog arrays are invalid");
    }
    if (!materials || !reactions || !phases || materials->size() > 65535U ||
        reactions->size() > 1'000'000U || phases->size() > 1'000'000U)
        return malformed("material Catalog arrays are missing or exceed their budgets");

    std::vector<MaterialDefinition> definitions;
    definitions.reserve(materials->size());
    for (std::size_t index = 0; index < materials->size(); ++index) {
        Object::Ptr value;
        try {
            value = materials->getObject(unsigned(index));
        } catch (...) {
            return malformed("material entry is not an object",
                             "materials[" + std::to_string(index) + "]");
        }
        if (!value || !exactFields(*value, {"id", "name", "state", "density", "viscosity",
                                            "thermalConductivity", "heatCapacity", "ignitionTemperature",
                                            "meltingTemperature", "boilingTemperature", "defaultLifetime",
                                            "flammable", "defaultTemperature", "blastResistance",
                                            "displayRgba", "tags"}))
            return malformed("material contains missing or unknown fields",
                             "materials[" + std::to_string(index) + "]");
        MaterialDefinition material;
        std::uint16_t id = 0;
        if (!readNumber(*value, "id", id) || !readNumber(*value, "density", material.density) ||
            !readNumber(*value, "viscosity", material.viscosity) ||
            !readNumber(*value, "thermalConductivity", material.thermalConductivity) ||
            !readNumber(*value, "heatCapacity", material.heatCapacity) ||
            !readNumber(*value, "ignitionTemperature", material.ignitionTemperature) ||
            !readNumber(*value, "meltingTemperature", material.meltingTemperature) ||
            !readNumber(*value, "boilingTemperature", material.boilingTemperature) ||
            !readNumber(*value, "defaultLifetime", material.defaultLifetime) ||
            !readNumber(*value, "defaultTemperature", material.defaultTemperature) ||
            !readNumber(*value, "blastResistance", material.blastResistance) ||
            !readNumber(*value, "displayRgba", material.displayRgba))
            return malformed("material contains an invalid number",
                             "materials[" + std::to_string(index) + "]");
        material.id = MaterialId(id);
        material.name = value->optValue<std::string>("name", "");
        if (!parseState(value->optValue<std::string>("state", ""), material.state))
            return malformed("material state is unknown",
                             "materials[" + std::to_string(index) + "].state");
        if (!value->has("flammable") || !value->has("tags"))
            return malformed("material fields are missing",
                             "materials[" + std::to_string(index) + "]");
        try {
            material.flammable = value->getValue<bool>("flammable");
            Array::Ptr tags = value->getArray("tags");
            if (!tags || tags->size() > 1024U) throw std::runtime_error("tags");
            for (std::size_t tag = 0; tag < tags->size(); ++tag)
                material.tags.push_back(tags->getElement<std::string>(unsigned(tag)));
        } catch (...) {
            return malformed("material tags or flammable value are invalid",
                             "materials[" + std::to_string(index) + "]");
        }
        definitions.push_back(std::move(material));
    }

    std::vector<MaterialReactionRule> reactionRules;
    reactionRules.reserve(reactions->size());
    for (std::size_t index = 0; index < reactions->size(); ++index) {
        Object::Ptr value;
        try {
            value = reactions->getObject(unsigned(index));
        } catch (...) {
            return malformed("reaction entry is not an object",
                             "reactions[" + std::to_string(index) + "]");
        }
        if (!value || !exactFields(*value, {"id", "first", "second", "firstResult", "secondResult",
                                            "minimumTemperature", "heatDelta", "priority"}))
            return malformed("reaction contains missing or unknown fields",
                             "reactions[" + std::to_string(index) + "]");
        MaterialReactionRule rule;
        std::uint16_t first = 0, second = 0, firstResult = 0, secondResult = 0;
        rule.id = value->optValue<std::string>("id", "");
        if (!readNumber(*value, "first", first) || !readNumber(*value, "second", second) ||
            !readNumber(*value, "firstResult", firstResult) ||
            !readNumber(*value, "secondResult", secondResult) ||
            !readNumber(*value, "minimumTemperature", rule.minimumTemperature) ||
            !readNumber(*value, "heatDelta", rule.heatDelta) ||
            !readNumber(*value, "priority", rule.priority))
            return malformed("reaction contains an invalid number",
                             "reactions[" + std::to_string(index) + "]");
        rule.first = MaterialId(first);
        rule.second = MaterialId(second);
        rule.firstResult = MaterialId(firstResult);
        rule.secondResult = MaterialId(secondResult);
        reactionRules.push_back(std::move(rule));
    }

    std::vector<MaterialPhaseRule> phaseRules;
    phaseRules.reserve(phases->size());
    for (std::size_t index = 0; index < phases->size(); ++index) {
        Object::Ptr value;
        try {
            value = phases->getObject(unsigned(index));
        } catch (...) {
            return malformed("phase rule entry is not an object",
                             "phaseRules[" + std::to_string(index) + "]");
        }
        if (!value || !exactFields(*value, {"id", "source", "result", "direction", "threshold",
                                            "temperatureDelta", "priority"}))
            return malformed("phase rule contains missing or unknown fields",
                             "phaseRules[" + std::to_string(index) + "]");
        MaterialPhaseRule rule;
        std::uint16_t source = 0, result = 0;
        rule.id = value->optValue<std::string>("id", "");
        const std::string direction = value->optValue<std::string>("direction", "");
        if (direction == "at_or_below") rule.direction = TemperatureDirection::AtOrBelow;
        else if (direction == "at_or_above") rule.direction = TemperatureDirection::AtOrAbove;
        else return malformed("phase direction is unknown",
                              "phaseRules[" + std::to_string(index) + "].direction");
        if (!readNumber(*value, "source", source) || !readNumber(*value, "result", result) ||
            !readNumber(*value, "threshold", rule.threshold) ||
            !readNumber(*value, "temperatureDelta", rule.temperatureDelta) ||
            !readNumber(*value, "priority", rule.priority))
            return malformed("phase rule contains an invalid number",
                             "phaseRules[" + std::to_string(index) + "]");
        rule.source = MaterialId(source);
        rule.result = MaterialId(result);
        phaseRules.push_back(std::move(rule));
    }
    return MaterialCatalog::create(std::move(definitions), std::move(reactionRules),
                                   std::move(phaseRules));
}

}  // namespace eve::pixelworld
