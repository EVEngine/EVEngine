#include "pixelworld/PixelWorldGenerationCodec.h"

#if defined(__EMSCRIPTEN__)

namespace eve::pixelworld {
namespace {

eve::Diagnostic webCodecUnavailable() {
    return eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                  "generation request JSON codec is unavailable on the Web build", "codec",
                                  {}, "pixelworld.generation-codec");
}

}  // namespace

eve::Result<std::string> encodePixelWorldGenerationRequestJson(
    const PixelWorldGenerationRequest&) {
    return eve::Result<std::string>::failure(webCodecUnavailable());
}

eve::Result<PixelWorldGenerationRequest> decodePixelWorldGenerationRequestJson(std::string_view) {
    return eve::Result<PixelWorldGenerationRequest>::failure(webCodecUnavailable());
}

}  // namespace eve::pixelworld

#else

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <charconv>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace eve::pixelworld {
namespace {

using Object = Poco::JSON::Object;
using Array = Poco::JSON::Array;

eve::Result<PixelWorldGenerationRequest> malformed(std::string message,
                                                   std::string path = "document") {
    return eve::Result<PixelWorldGenerationRequest>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::SerializationError, std::move(message), std::move(path), {},
        "pixelworld.generation-codec"));
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

bool readSeed(const Object& object, std::uint64_t& seed) {
    if (!object.has("seed") || object.isNull("seed")) return false;
    try {
        const std::string text = object.getValue<std::string>("seed");
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto parsed = std::from_chars(begin, end, seed);
        return parsed.ec == std::errc{} && parsed.ptr == end;
    } catch (...) {
        return false;
    }
}

Object::Ptr cellJson(const PixelCell& cell) {
    Object::Ptr out(new Object);
    out->set("material", std::uint16_t(cell.material));
    out->set("temperature", cell.temperature);
    out->set("lifetime", cell.lifetime);
    out->set("thermalRemainder", cell.thermalRemainder);
    return out;
}

}  // namespace

eve::Result<std::string> encodePixelWorldGenerationRequestJson(
    const PixelWorldGenerationRequest& request) {
    Object::Ptr root(new Object);
    root->set("schema", "eve.pixelworld.generation-request");
    root->set("version", request.schemaVersion);
    root->set("seed", std::to_string(request.seed));
    Object::Ptr region(new Object);
    region->set("minX", request.region.minX);
    region->set("minY", request.region.minY);
    region->set("maxX", request.region.maxX);
    region->set("maxY", request.region.maxY);
    root->set("region", region);
    root->set("surfaceY", request.surfaceY);
    root->set("terrainAmplitude", request.terrainAmplitude);
    root->set("waterLevel", request.waterLevel);
    root->set("caveThreshold", request.caveThreshold);
    Array::Ptr stamps(new Array);
    for (const PixelMaterialStamp& stamp : request.stamps) {
        Object::Ptr value(new Object);
        value->set("originX", stamp.originX);
        value->set("originY", stamp.originY);
        value->set("width", stamp.width);
        value->set("height", stamp.height);
        Array::Ptr cells(new Array);
        for (const PixelCell& cell : stamp.cells) cells->add(cellJson(cell));
        value->set("cells", cells);
        stamps->add(value);
    }
    root->set("stamps", stamps);
    std::ostringstream text;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(root), text, 2, -1);
    return eve::Result<std::string>::success(text.str());
}

eve::Result<PixelWorldGenerationRequest> decodePixelWorldGenerationRequestJson(
    std::string_view json) {
    Object::Ptr root;
    try {
        root = Poco::JSON::Parser().parse(std::string(json)).extract<Object::Ptr>();
    } catch (...) {
        return malformed("generation request JSON is malformed");
    }
    if (!root || !exactFields(*root, {"schema", "version", "seed", "region", "surfaceY",
                                      "terrainAmplitude", "waterLevel", "caveThreshold", "stamps"}))
        return malformed("root contains missing or unknown fields");
    if (root->optValue<std::string>("schema", "") != "eve.pixelworld.generation-request")
        return malformed("unknown generation request schema", "schema");

    PixelWorldGenerationRequest request;
    if (!readNumber(*root, "version", request.schemaVersion) ||
        request.schemaVersion != PixelWorldGenerationRequest::kSchemaVersion)
        return malformed("unknown generation request version", "version");
    if (!readSeed(*root, request.seed)) return malformed("seed is not a canonical uint64", "seed");
    Object::Ptr region;
    Array::Ptr stamps;
    try {
        region = root->getObject("region");
        stamps = root->getArray("stamps");
    } catch (...) {
        return malformed("region or stamps has an invalid type");
    }
    if (!region || !exactFields(*region, {"minX", "minY", "maxX", "maxY"}) || !stamps ||
        stamps->size() > 65'536U)
        return malformed("region fields or stamp budget are invalid");
    if (!readNumber(*region, "minX", request.region.minX) ||
        !readNumber(*region, "minY", request.region.minY) ||
        !readNumber(*region, "maxX", request.region.maxX) ||
        !readNumber(*region, "maxY", request.region.maxY) ||
        !readNumber(*root, "surfaceY", request.surfaceY) ||
        !readNumber(*root, "terrainAmplitude", request.terrainAmplitude) ||
        !readNumber(*root, "waterLevel", request.waterLevel) ||
        !readNumber(*root, "caveThreshold", request.caveThreshold))
        return malformed("generation request contains an invalid number");

    std::uint64_t totalCells = 0;
    request.stamps.reserve(stamps->size());
    for (std::size_t index = 0; index < stamps->size(); ++index) {
        Object::Ptr value;
        Array::Ptr cells;
        try {
            value = stamps->getObject(unsigned(index));
            if (value) cells = value->getArray("cells");
        } catch (...) {
            return malformed("stamp is not an object", "stamps[" + std::to_string(index) + "]");
        }
        if (!value || !exactFields(*value, {"originX", "originY", "width", "height", "cells"}) ||
            !cells)
            return malformed("stamp contains missing or unknown fields",
                             "stamps[" + std::to_string(index) + "]");
        PixelMaterialStamp stamp;
        if (!readNumber(*value, "originX", stamp.originX) ||
            !readNumber(*value, "originY", stamp.originY) ||
            !readNumber(*value, "width", stamp.width) ||
            !readNumber(*value, "height", stamp.height) || stamp.width <= 0 || stamp.height <= 0 ||
            std::uint64_t(stamp.width) * std::uint64_t(stamp.height) != cells->size())
            return malformed("stamp dimensions are invalid", "stamps[" + std::to_string(index) + "]");
        totalCells += cells->size();
        if (totalCells > 4'194'304ULL) return malformed("stamp cell budget is exceeded", "stamps");
        stamp.cells.reserve(cells->size());
        for (std::size_t cellIndex = 0; cellIndex < cells->size(); ++cellIndex) {
            Object::Ptr item;
            try {
                item = cells->getObject(unsigned(cellIndex));
            } catch (...) {
                return malformed("stamp cell is not an object", "stamps.cells");
            }
            PixelCell cell;
            std::uint16_t material = 0;
            if (!item || !exactFields(*item, {"material", "temperature", "lifetime", "thermalRemainder"}) ||
                !readNumber(*item, "material", material) ||
                !readNumber(*item, "temperature", cell.temperature) ||
                !readNumber(*item, "lifetime", cell.lifetime) ||
                !readNumber(*item, "thermalRemainder", cell.thermalRemainder))
                return malformed("stamp cell contains invalid fields", "stamps.cells");
            cell.material = MaterialId(material);
            stamp.cells.push_back(cell);
        }
        request.stamps.push_back(std::move(stamp));
    }
    return eve::Result<PixelWorldGenerationRequest>::success(std::move(request));
}

}  // namespace eve::pixelworld

#endif  // defined(__EMSCRIPTEN__)
