#include "common/SceneLocalRegion.h"
#include <cmath>
namespace eve {
Result<SceneLocalRegion> SceneLocalRegion::fromValue(const Value& value) {
    const auto invalid = [] {
        return Result<SceneLocalRegion>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Region requires eve.scene.local-region v1 local_box, id, geometryRevision and "
                              "finite increasing bounds"));
    };
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != 7) return invalid();
    for (const auto* key : {"schemaId", "schemaVersion", "kind", "id", "geometryRevision", "minimum", "maximum"})
        if (!object->contains(key)) return invalid();
    const auto* schema   = object->at("schemaId").getIf<std::string>();
    const auto* version  = object->at("schemaVersion").getIf<std::int64_t>();
    const auto* kind     = object->at("kind").getIf<std::string>();
    const auto* id       = object->at("id").getIf<std::string>();
    const auto* revision = object->at("geometryRevision").getIf<std::string>();
    const auto* lower    = object->at("minimum").getIf<Value::Array>();
    const auto* upper    = object->at("maximum").getIf<Value::Array>();
    if (!schema || *schema != "eve.scene.local-region" || !version || *version != 1 || !kind || *kind != "local_box" ||
        !id || id->empty() || id->size() > 256 || !revision || revision->empty() || revision->size() > 16384 ||
        !lower || !upper || lower->size() != 3 || upper->size() != 3)
        return invalid();
    SceneLocalRegion region;
    region.id               = *id;
    region.geometryRevision = *revision;
    for (int i = 0; i < 3; ++i) {
        const auto numeric = [](const Value& value) {
            if (auto real = value.getIf<double>()) return *real;
            if (auto integer = value.getIf<std::int64_t>()) return double(*integer);
            return double(NAN);
        };
        region.minimum[i] = numeric((*lower)[i]);
        region.maximum[i] = numeric((*upper)[i]);
        if (!std::isfinite(region.minimum[i]) || !std::isfinite(region.maximum[i]) ||
            std::abs(region.minimum[i]) > 1e8 || std::abs(region.maximum[i]) > 1e8 ||
            region.minimum[i] >= region.maximum[i])
            return invalid();
    }
    return Result<SceneLocalRegion>::success(std::move(region));
}
Value SceneLocalRegion::toValue() const {
    return Value::Object{{"schemaId", "eve.scene.local-region"},
                         {"schemaVersion", 1},
                         {"kind", "local_box"},
                         {"id", id},
                         {"geometryRevision", geometryRevision},
                         {"minimum", Value::Array{minimum[0], minimum[1], minimum[2]}},
                         {"maximum", Value::Array{maximum[0], maximum[1], maximum[2]}}};
}
}  // namespace eve
