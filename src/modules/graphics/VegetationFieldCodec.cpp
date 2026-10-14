#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include "graphics/VegetationField.h"

namespace eve::graphics {
namespace {
using Object = Value::Object;
void fields(const Value& v, std::initializer_list<std::string_view> names) {
    if (!v.isObject() || v.getIf<Value::Object>()->size() != names.size())
        throw std::invalid_argument("unexpected field set");
    for (auto name : names)
        if (!v.find(std::string(name))) throw std::invalid_argument("missing field " + std::string(name));
}
const Value& at(const Value& v, std::string_view name) { return v.getIf<Value::Object>()->at(std::string(name)); }
float        number(const Value& v) {
    if (!v.isInt64() && !v.isDouble()) throw std::invalid_argument("expected finite number");
    const double d = v.isDouble() ? v.asDouble() : double(v.asInt());
    const float  f = float(d);
    if (!std::isfinite(f)) throw std::invalid_argument("expected finite float");
    return f;
}
int integer(const Value& v, int low, int high) {
    if (!v.isInt64() || v.asInt() < low || v.asInt() > high) throw std::invalid_argument("integer out of range");
    return int(v.asInt());
}
glm::vec4 vector(const Value& v, size_t count) {
    if (!v.isArray() || v.getIf<Value::Array>()->size() != count) throw std::invalid_argument("invalid vector length");
    glm::vec4 result(0.f);
    for (size_t i = 0; i < count; ++i) result[int(i)] = number((*v.getIf<Value::Array>())[i]);
    return result;
}
Value encode(glm::vec4 v) { return Value::array({v.x, v.y, v.z, v.w}); }
Value encode(glm::vec3 v) { return Value::array({v.x, v.y, v.z}); }
}  // namespace

Result<void> VegetationField::restore(const Value& document) {
    try {
        fields(document, {"schema", "version", "globals", "elements"});
        const auto& schema = at(document, "schema");
        if (!schema.isString() || schema.asString() != "eve.graphics.vegetation-field")
            throw std::invalid_argument("unexpected vegetation schema");
        const auto& version = at(document, "version");
        if (!version.isInt64() || version.asInt() != 1)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::UnknownVersion, "only vegetation field version 1 is supported"));
        const auto& g = at(document, "globals");
        fields(g, {"color", "extras", "motion", "vertex", "season"});
        VegetationGlobals globals;
        globals.color      = vector(at(g, "color"), 4);
        globals.extras     = vector(at(g, "extras"), 4);
        globals.motion     = vector(at(g, "motion"), 4);
        globals.vertex     = vector(at(g, "vertex"), 4);
        globals.season     = number(at(g, "season"));
        const auto& source = at(document, "elements");
        if (!source.isArray() || source.getIf<Value::Array>()->size() > 4096)
            throw std::invalid_argument("invalid element count");
        std::vector<VegetationElement> elements;
        size_t                         totalPixels = 0;
        for (const auto& entry : (*source.getIf<Value::Array>())) {
            fields(entry, {"channel", "blend", "shape", "center", "extents", "yaw", "opacity", "edgeFade", "layers",
                           "priority", "value", "seasonal", "seasons", "mask"});
            VegetationElement e;
            e.channel  = VegetationChannel(integer(at(entry, "channel"), 0, 3));
            e.blend    = VegetationBlend(integer(at(entry, "blend"), 0, 4));
            e.shape    = VegetationShape(integer(at(entry, "shape"), 0, 1));
            e.center   = glm::vec3(vector(at(entry, "center"), 3));
            e.extents  = glm::vec3(vector(at(entry, "extents"), 3));
            e.yaw      = number(at(entry, "yaw"));
            e.opacity  = number(at(entry, "opacity"));
            e.edgeFade = number(at(entry, "edgeFade"));
            e.layers   = uint16_t(integer(at(entry, "layers"), 1, 511));
            e.priority = integer(at(entry, "priority"), INT32_MIN, INT32_MAX);
            e.value    = vector(at(entry, "value"), 4);
            if (!at(entry, "seasonal").isBool()) throw std::invalid_argument("seasonal must be boolean");
            e.seasonal          = at(entry, "seasonal").asBool();
            const auto& seasons = at(entry, "seasons");
            if (!seasons.isArray() || seasons.getIf<Value::Array>()->size() != 4)
                throw std::invalid_argument("four seasons required");
            for (size_t i = 0; i < 4; ++i) e.seasons[i] = vector((*seasons.getIf<Value::Array>())[i], 4);
            const auto& mask = at(entry, "mask");
            fields(mask, {"width", "height", "pixels"});
            e.mask.width       = uint32_t(integer(at(mask, "width"), 0, 2048));
            e.mask.height      = uint32_t(integer(at(mask, "height"), 0, 2048));
            const auto& pixels = at(mask, "pixels");
            if (!pixels.isArray() || pixels.getIf<Value::Array>()->size() != size_t(e.mask.width) * e.mask.height)
                throw std::invalid_argument("mask pixel count mismatch");
            totalPixels += pixels.getIf<Value::Array>()->size();
            if (totalPixels > 4 * 1024 * 1024) throw std::invalid_argument("mask pixel budget exceeded");
            for (const auto& pixel : (*pixels.getIf<Value::Array>())) e.mask.pixels.push_back(vector(pixel, 4));
            elements.push_back(std::move(e));
        }
        return replace(globals, elements);
    } catch (const std::invalid_argument& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), {}, {}, "graphics.vegetation"));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "vegetation restore allocation failed"));
    }
}

Result<Value> VegetationField::snapshot() const {
    try {
        Value::Array elements;
        for (const auto& e : elements_) {
            Value::Array seasons, pixels;
            for (auto season : e.seasons) seasons.push_back(encode(season));
            for (auto pixel : e.mask.pixels) pixels.push_back(encode(pixel));
            elements.push_back(Value::object({{"channel", int(e.channel)},
                                              {"blend", int(e.blend)},
                                              {"shape", int(e.shape)},
                                              {"center", encode(e.center)},
                                              {"extents", encode(e.extents)},
                                              {"yaw", e.yaw},
                                              {"opacity", e.opacity},
                                              {"edgeFade", e.edgeFade},
                                              {"layers", int(e.layers)},
                                              {"priority", e.priority},
                                              {"value", encode(e.value)},
                                              {"seasonal", e.seasonal},
                                              {"seasons", Value(std::move(seasons))},
                                              {"mask", Value::object({{"width", int(e.mask.width)},
                                                                      {"height", int(e.mask.height)},
                                                                      {"pixels", Value(std::move(pixels))}})}}));
        }
        return Result<Value>::success(Value::object({{"schema", "eve.graphics.vegetation-field"},
                                                     {"version", 1},
                                                     {"globals", Value::object({{"color", encode(globals_.color)},
                                                                                {"extras", encode(globals_.extras)},
                                                                                {"motion", encode(globals_.motion)},
                                                                                {"vertex", encode(globals_.vertex)},
                                                                                {"season", globals_.season}})},
                                                     {"elements", Value(std::move(elements))}}));
    } catch (const std::bad_alloc&) {
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation snapshot allocation failed"));
    }
}
}  // namespace eve::graphics
