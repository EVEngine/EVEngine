#include "fluids/OceanInteractionCodec.h"
#include "fluids/OceanCodecInternal.h"

namespace eve::fluids {
using namespace ocean_codec;
Value encodeOceanWakeSettings(const OceanWakeSettings& s) {
    return Value::Object{{"resolution", Value(static_cast<std::int64_t>(s.resolution))},
                         {"length", Value(s.length)},
                         {"depth", Value(s.depth)},
                         {"gravity", Value(s.gravity)},
                         {"damping", Value(s.damping)},
                         {"absorptionWidth", Value(s.absorptionWidth)},
                         {"foamDecay", Value(s.foamDecay)},
                         {"center", vector(s.center)}};
}
Result<OceanWakeSettings> decodeOceanWakeSettings(const Value& value) {
    try {
        keys(value, {"resolution", "length", "depth", "gravity", "damping", "absorptionWidth", "foamDecay", "center"});
        OceanWakeSettings out;
        const auto&       resolution = field(value, "resolution");
        if (!resolution.isInt64() || resolution.asInt() < 8 || resolution.asInt() > 512)
            throw std::runtime_error("bounded integer resolution");
        out.resolution      = static_cast<unsigned>(resolution.asInt());
        out.length          = number(field(value, "length"));
        out.depth           = number(field(value, "depth"));
        out.gravity         = number(field(value, "gravity"));
        out.damping         = number(field(value, "damping"));
        out.absorptionWidth = number(field(value, "absorptionWidth"));
        out.foamDecay       = number(field(value, "foamDecay"));
        out.center          = vector(field(value, "center"));
        return Result<OceanWakeSettings>::success(out);
    } catch (const std::runtime_error& error) {
        return Result<OceanWakeSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean wake arguments", error.what()));
    }
}
Value encodeOceanHullSettings(const OceanHullSettings& s) {
    Value::Array cells;
    for (const auto& c : s.cells)
        cells.emplace_back(
            Value::Object{{"center", vector(c.center)}, {"radius", Value(c.radius)}, {"volume", Value(c.volume)}});
    return Value::Object{{"cells", Value(std::move(cells))},
                         {"waterDensity", Value(s.waterDensity)},
                         {"gravity", Value(s.gravity)},
                         {"drag", vector(s.drag)},
                         {"propeller", vector(s.propeller)},
                         {"thrust", Value(s.thrust)},
                         {"rudderCoefficient", Value(s.rudderCoefficient)}};
}
Result<OceanHullSettings> decodeOceanHullSettings(const Value& value) {
    try {
        keys(value, {"cells", "waterDensity", "gravity", "drag", "propeller", "thrust", "rudderCoefficient"});
        OceanHullSettings out;
        out.waterDensity      = number(field(value, "waterDensity"));
        out.gravity           = number(field(value, "gravity"));
        out.drag              = vector3(field(value, "drag"));
        out.propeller         = vector3(field(value, "propeller"));
        out.thrust            = number(field(value, "thrust"));
        out.rudderCoefficient = number(field(value, "rudderCoefficient"));
        const auto& cells     = field(value, "cells");
        if (!cells.isArray() || cells.arraySize() > 64) throw std::runtime_error("bounded hull cells");
        for (std::size_t i = 0; i < cells.arraySize(); ++i) {
            const auto& c = cells.at(i);
            keys(c, {"center", "radius", "volume"});
            out.cells.push_back({vector3(field(c, "center")), number(field(c, "radius")), number(field(c, "volume"))});
        }
        return Result<OceanHullSettings>::success(std::move(out));
    } catch (const std::runtime_error& error) {
        return Result<OceanHullSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean hull arguments", error.what()));
    }
}
}  // namespace eve::fluids
