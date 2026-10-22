#include "fluids/OceanSpectrumCodec.h"
#include "fluids/OceanCodecInternal.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace eve::fluids {
using namespace ocean_codec;

Value encodeOceanSpectrumSettings(const OceanSpectrumSettings& s) {
    Value::Array bands;
    for (const auto& b : s.bands)
        bands.emplace_back(Value::Object{{"resolution", Value(static_cast<std::int64_t>(b.resolution))},
                                         {"length", Value(b.length)},
                                         {"minimumWavelength", Value(b.minimumWavelength)},
                                         {"maximumWavelength", Value(b.maximumWavelength)}});
    return Value::Object{
        {"schema", Value("eve.ocean.spectrum")}, {"version", Value(1)},
        {"seed", Value(std::to_string(s.seed))}, {"significantWaveHeight", Value(s.significantWaveHeight)},
        {"peakPeriod", Value(s.peakPeriod)},     {"peakEnhancement", Value(s.peakEnhancement)},
        {"gravity", Value(s.gravity)},           {"seaLevel", Value(s.seaLevel)},
        {"choppiness", Value(s.choppiness)},     {"windDirection", vector(s.windDirection)},
        {"current", vector(s.current)},          {"bands", Value(std::move(bands))}};
}

Result<OceanSpectrumSettings> decodeOceanSpectrumSettings(const Value& value) {
    try {
        keys(value, {"schema", "version", "seed", "significantWaveHeight", "peakPeriod", "peakEnhancement", "gravity",
                     "seaLevel", "choppiness", "windDirection", "current", "bands"});
        const auto& schema  = field(value, "schema");
        const auto& version = field(value, "version");
        if (!schema.isString() || schema.asString() != "eve.ocean.spectrum" || !version.isInt64() ||
            version.asInt() != 1)
            throw std::runtime_error("unsupported schema/version");
        OceanSpectrumSettings out;
        const auto&           seed = field(value, "seed");
        if (!seed.isString()) throw std::runtime_error("seed requires canonical decimal text");
        const auto& text = seed.asString();
        if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0'))
            throw std::runtime_error("noncanonical seed");
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out.seed);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::runtime_error("seed outside uint64");
        out.significantWaveHeight = number(field(value, "significantWaveHeight"));
        out.peakPeriod            = number(field(value, "peakPeriod"));
        out.peakEnhancement       = number(field(value, "peakEnhancement"));
        out.gravity               = number(field(value, "gravity"));
        out.seaLevel              = number(field(value, "seaLevel"));
        out.choppiness            = number(field(value, "choppiness"));
        out.windDirection         = vector(field(value, "windDirection"));
        out.current               = vector(field(value, "current"));
        const auto& bands         = field(value, "bands");
        if (!bands.isArray() || bands.arraySize() == 0 || bands.arraySize() > 4) throw std::runtime_error("band count");
        out.bands.clear();
        for (std::size_t i = 0; i < bands.arraySize(); ++i) {
            const auto& b = bands.at(i);
            keys(b, {"resolution", "length", "minimumWavelength", "maximumWavelength"});
            const auto& resolution = field(b, "resolution");
            if (!resolution.isInt64() || resolution.asInt() < 0 ||
                resolution.asInt() > std::numeric_limits<unsigned>::max())
                throw std::runtime_error("resolution integer range");
            out.bands.push_back({static_cast<unsigned>(resolution.asInt()), number(field(b, "length")),
                                 number(field(b, "minimumWavelength")), number(field(b, "maximumWavelength"))});
        }
        return Result<OceanSpectrumSettings>::success(std::move(out));
    } catch (const std::runtime_error& error) {
        return Result<OceanSpectrumSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean spectrum definition", error.what()));
    }
}
}  // namespace eve::fluids
