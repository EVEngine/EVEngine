#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include "common/Value.h"
#include "filesystem/FileData.h"
#include "filesystem/PreparedFile.h"
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsAssetData.h"
#include "graphics/sky/SkyWispsLayer.h"

namespace eve::graphics {
namespace {
void fields(const Value& value, std::initializer_list<std::string_view> expected) {
    if (!value.isObject() || value.keys().size() != expected.size())
        throw std::runtime_error("Invalid wisps object fields");
    for (const auto& key : value.keys())
        if (std::find(expected.begin(), expected.end(), key) == expected.end())
            throw std::runtime_error("Unknown wisps field: " + key);
}
const Value& field(const Value& value, const char* name) {
    const auto* result = value.find(name);
    if (!result) throw std::runtime_error(std::string("Missing wisps field: ") + name);
    return *result;
}
std::string text(const Value& value, const char* name) {
    const auto& result = field(value, name);
    if (!result.isString()) throw std::runtime_error("Expected wisps string");
    return result.asString();
}
float number(const Value& value) {
    if (!value.isNumeric()) throw std::runtime_error("Expected wisps number");
    const double v = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(v) || std::abs(v) > 1e8) throw std::runtime_error("Wisps number outside supported range");
    return static_cast<float>(v);
}
template <size_t N>
std::array<float, N> vector(const Value& value) {
    if (!value.isArray() || value.arraySize() != N) throw std::runtime_error("Invalid wisps vector size");
    std::array<float, N> result{};
    for (size_t i = 0; i < N; ++i) result[i] = number(value.at(i));
    return result;
}
bool boolean(const Value& value) {
    if (!value.isBool()) throw std::runtime_error("Expected wisps boolean");
    return value.asBool();
}
std::shared_ptr<const filesystem::FileData> bytes(const std::string& path, size_t limit) {
    auto result = filesystem::readPreparedFile(path, limit);
    if (!result) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
Value document(const std::string& path, size_t limit) {
    auto file   = bytes(path, limit);
    auto parsed = Value::fromJson(std::string(static_cast<const char*>(file->getData()), file->getSize()));
    if (!parsed) throw std::runtime_error(parsed.status().describe());
    return std::move(parsed).takeValue();
}
std::string resourcePath(const std::string& manifest, const std::string& relative) {
    if (relative.empty() || relative.find_first_of("\\:") != std::string::npos)
        throw std::runtime_error("Invalid relative wisps resource path");
    const std::filesystem::path p(relative);
    if (p.is_absolute()) throw std::runtime_error("Wisps resource must be relative");
    for (const auto& part : p)
        if (part == ".." || part == ".") throw std::runtime_error("Wisps resource escapes package");
    return (std::filesystem::path(manifest).parent_path() / p).generic_string();
}
}  // namespace
SkyWispsAsset::SkyWispsAsset(std::shared_ptr<const Impl> data) : data_(std::move(data)) {}
const SkyWispsLayer&  SkyWispsAsset::layer() const noexcept { return data_->layer; }
Result<SkyWispsAsset> SkyWispsAsset::load(const std::string& manifestPath) {
    try {
        const auto manifest = document(manifestPath, 65536);
        const auto schema   = text(manifest, "schema");
        const bool lunar    = schema == "eve.sky-wisps/7";
        const bool packed   = lunar || schema == "eve.sky-wisps/6";
        if (lunar)
            fields(manifest,
                   {"schema", "mesh", "density", "material", "contrast", "sunDisk", "sunDiskCurve", "daylight", "stars",
                    "starsNoise", "optics", "moonDisk", "moonColorTexture", "moonNormalTexture"});
        else if (schema == "eve.sky-wisps/1")
            fields(manifest, {"schema", "mesh", "density", "material"});
        else if (schema == "eve.sky-wisps/2")
            fields(manifest, {"schema", "mesh", "density", "material", "contrast"});
        else if (schema == "eve.sky-wisps/3")
            fields(manifest, {"schema", "mesh", "density", "material", "contrast", "sunDisk"});
        else if (schema == "eve.sky-wisps/4")
            fields(manifest, {"schema", "mesh", "density", "material", "contrast", "sunDisk", "sunDiskCurve"});
        else if (schema == "eve.sky-wisps/5")
            fields(manifest, {"schema", "mesh", "density", "material", "contrast", "sunDisk", "sunDiskCurve",
                              "daylight", "stars", "starsNoise"});
        else if (schema == "eve.sky-wisps/6")
            fields(manifest, {"schema", "mesh", "density", "material", "contrast", "sunDisk", "sunDiskCurve",
                              "daylight", "stars", "starsNoise", "optics"});
        else
            throw std::runtime_error("Unsupported wisps schema");
        auto       result = std::make_shared<Impl>();
        const auto mesh   = document(resourcePath(manifestPath, text(manifest, "mesh")), 32 * 1024 * 1024);
        fields(mesh, {"schema", "space", "uvChannel", "layout", "topology", "vertices", "sourceSha256"});
        if (text(mesh, "schema") != "eve.sky-mesh/1" || text(mesh, "space") != "unreal-local-centimetres" ||
            text(mesh, "topology") != "triangle-list" || !field(mesh, "uvChannel").isInt64() ||
            field(mesh, "uvChannel").asInt() != 3)
            throw std::runtime_error("Unsupported wisps mesh contract");
        const auto hash = text(mesh, "sourceSha256");
        if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid source provenance hash");
        const auto&           layout = field(mesh, "layout");
        constexpr const char* names[]{"x", "y", "z", "u", "v", "r", "g", "b", "a"};
        if (!layout.isArray() || layout.arraySize() != 9) throw std::runtime_error("Invalid mesh layout");
        for (size_t i = 0; i < 9; ++i)
            if (!layout.at(i).isString() || layout.at(i).asString() != names[i])
                throw std::runtime_error("Unsupported mesh layout");
        const auto& vertices = field(mesh, "vertices");
        if (!vertices.isArray() || vertices.arraySize() == 0 || vertices.arraySize() % 3 ||
            vertices.arraySize() > 1000000)
            throw std::runtime_error("Invalid sky mesh corner count");
        result->corners.reserve(vertices.arraySize() * 9);
        for (size_t i = 0; i < vertices.arraySize(); ++i) {
            const auto corner = vector<9>(vertices.at(i));
            result->corners.insert(result->corners.end(), corner.begin(), corner.end());
        }
        const auto& density = field(manifest, "density");
        fields(density, {"path", "width", "height", "encoding"});
        const auto dimension = [&](const char* name) {
            const auto& n = field(density, name);
            if (!n.isInt64() || n.asInt() < 1 || n.asInt() > 4096)
                throw std::runtime_error("Invalid wisps texture dimensions");
            return static_cast<uint32_t>(n.asInt());
        };
        auto& image         = result->texture;
        image.width         = dimension("width");
        image.height        = dimension("height");
        const auto encoding = text(density, "encoding");
        if (encoding != "r8-linear" && encoding != "r8-srgb")
            throw std::runtime_error("Unsupported wisps density encoding");
        auto pixels = bytes(resourcePath(manifestPath, text(density, "path")), 16 * 1024 * 1024);
        if (pixels->getSize() != size_t(image.width) * image.height)
            throw std::runtime_error("Truncated wisps density texture");
        const auto* source = static_cast<const std::byte*>(pixels->getData());
        if (encoding == "r8-srgb") {
            image.format = ShaderImageFormat::RGBA8Srgb;
            result->pixels.resize(pixels->getSize() * 4);
            for (size_t i = 0; i < pixels->getSize(); ++i) {
                result->pixels[i * 4] = result->pixels[i * 4 + 1] = result->pixels[i * 4 + 2] = source[i];
                result->pixels[i * 4 + 3]                                                     = std::byte{255};
            }
        } else {
            image.format = ShaderImageFormat::R8;
            result->pixels.assign(source, source + pixels->getSize());
        }
        image.bytes           = result->pixels;
        image.sampler         = TextureSampler::linear();
        image.sampler.repeatU = image.sampler.repeatV = true;
        const auto& material                          = field(manifest, "material");
        fields(material, {"domeScale", "color", "gradient", "movement", "moonForward", "morphRate", "morphPeriod",
                          "morphAmount", "opacity", "litIntensity", "cloudTime", "moonGradient", "staticClouds"});
        auto& w = result->layer;
        if (schema != "eve.sky-wisps/1") w.materialContrast = vector<3>(field(manifest, "contrast"));
        w.authoredSkyLighting = schema != "eve.sky-wisps/1";
        if (packed || schema == "eve.sky-wisps/3" || schema == "eve.sky-wisps/4" || schema == "eve.sky-wisps/5") {
            const auto& disk = field(manifest, "sunDisk");
            fields(disk, {"color", "shape"});
            w.sunDiskColor = vector<3>(field(disk, "color"));
            w.sunDiskShape = vector<3>(field(disk, "shape"));
            if (*std::min_element(w.sunDiskColor.begin(), w.sunDiskColor.end()) < 0 || w.sunDiskShape[0] <= 0 ||
                w.sunDiskShape[0] > 3.141593f || w.sunDiskShape[1] <= 0 || w.sunDiskShape[2] < 0)
                throw std::runtime_error("Invalid solar disk radiance or shape");
        }
        if (packed || schema == "eve.sky-wisps/4" || schema == "eve.sky-wisps/5") {
            const auto& curve = field(manifest, "sunDiskCurve");
            if (!curve.isArray() || curve.arraySize() != 12)
                throw std::runtime_error("Solar curve requires twelve RGB keys");
            for (size_t i = 0; i < 12; ++i) {
                auto key = vector<4>(curve.at(i));
                if (key[0] < 0 || key[0] > 1 || key[1] < 0 || (i % 4 && key[0] <= w.sunDiskCurve[i - 1][0]) ||
                    (i % 4 == 3 && key[1] <= 0))
                    throw std::runtime_error("Invalid solar curve key order or value");
                w.sunDiskCurve[i] = key;
            }
            w.sunDiskCurveEnabled = true;
        }
        if (schema == "eve.sky-wisps/5" || packed) {
            const auto& cycle = field(manifest, "daylight");
            fields(cycle, {"sun", "moon", "moonOrbit", "dayTint", "duskTint", "nightTint", "glow", "controls", "stars",
                           "starUv", "twinkle", "scatteringCurve"});
            auto& d          = w.daylight;
            d.sun            = vector<4>(field(cycle, "sun"));
            d.moon           = vector<4>(field(cycle, "moon"));
            d.moonOrbit      = vector<4>(field(cycle, "moonOrbit"));
            d.dayTint        = vector<4>(field(cycle, "dayTint"));
            d.duskTint       = vector<4>(field(cycle, "duskTint"));
            d.nightTint      = vector<4>(field(cycle, "nightTint"));
            d.glow           = vector<4>(field(cycle, "glow"));
            d.controls       = vector<4>(field(cycle, "controls"));
            d.stars          = vector<4>(field(cycle, "stars"));
            d.starUv         = vector<4>(field(cycle, "starUv"));
            d.twinkle        = vector<4>(field(cycle, "twinkle"));
            const auto& keys = field(cycle, "scatteringCurve");
            if (!keys.isArray() || keys.arraySize() != d.scatteringCurve.size())
                throw std::runtime_error("Scattering curve requires 21 shared angle/RGB keys");
            for (size_t i = 0; i < d.scatteringCurve.size(); ++i) d.scatteringCurve[i] = vector<4>(keys.at(i));
            if (packed) {
                const auto& optical = field(manifest, "optics");
                fields(optical, {"rayDay", "rayDusk", "rayNight", "absorptionDay", "absorptionNight"});
                d.rayDay              = vector<4>(field(optical, "rayDay"));
                d.rayDusk             = vector<4>(field(optical, "rayDusk"));
                d.rayNight            = vector<4>(field(optical, "rayNight"));
                d.absorptionDay       = vector<4>(field(optical, "absorptionDay"));
                d.absorptionNight     = vector<4>(field(optical, "absorptionNight"));
                w.opticalCycleEnabled = true;
            }
            if (lunar) {
                const auto& disk = field(manifest, "moonDisk");
                fields(disk, {"color", "shape", "lighting", "glow"});
                d.moonDiskColor    = vector<4>(field(disk, "color"));
                d.moonDiskShape    = vector<4>(field(disk, "shape"));
                d.moonDiskLighting = vector<4>(field(disk, "lighting"));
                d.moonDiskGlow     = vector<4>(field(disk, "glow"));
            }
            auto checked = d.validate();
            if (!checked) throw std::runtime_error(checked.status().describe());
            const std::array names{"stars", "starsNoise", "moonColorTexture", "moonNormalTexture"};
            for (size_t i = 0; i < (lunar ? 4u : 2u); ++i) {
                const auto& descriptor = field(manifest, names[i]);
                if (packed)
                    fields(descriptor, {"path", "width", "height", "encoding", "mipLevels"});
                else
                    fields(descriptor, {"path", "width", "height", "encoding"});
                auto&      star   = result->celestialImages[i];
                const auto extent = [&](const char* key) {
                    const auto& v = field(descriptor, key);
                    if (!v.isInt64() || v.asInt() < 1 || v.asInt() > 4096)
                        throw std::runtime_error("Invalid star extent");
                    return static_cast<uint32_t>(v.asInt());
                };
                star.width        = extent("width");
                star.height       = extent("height");
                const auto format = text(descriptor, "encoding");
                if (format != "rgba8-srgb" && format != "rgba8-linear" && !(packed && format == "rgba16-float"))
                    throw std::runtime_error("Invalid star encoding");
                star.format = format == "rgba8-srgb" ? ShaderImageFormat::RGBA8Srgb : ShaderImageFormat::RGBA8;
                if (format == "rgba16-float") star.format = ShaderImageFormat::RGBA16Float;
                if (packed) star.mipLevels = extent("mipLevels");
                unsigned maximumMips = 1;
                for (unsigned size = std::max(star.width, star.height); size > 1; size >>= 1) ++maximumMips;
                if (star.mipLevels > maximumMips) throw std::runtime_error("Invalid star mip count");
                size_t expected = 0;
                for (unsigned mip = 0; mip < star.mipLevels; ++mip)
                    expected += size_t(std::max(1u, star.width >> mip)) * std::max(1u, star.height >> mip) *
                                (format == "rgba16-float" ? 8u : 4u);
                auto data = bytes(resourcePath(manifestPath, text(descriptor, "path")), 64 * 1024 * 1024);
                if (data->getSize() != expected) throw std::runtime_error("Truncated star texture");
                const auto* begin = static_cast<const std::byte*>(data->getData());
                result->celestialPixels[i].assign(begin, begin + data->getSize());
                star.bytes           = result->celestialPixels[i];
                star.sampler         = star.mipLevels > 1 ? TextureSampler::linearMipmap() : TextureSampler::linear();
                star.sampler.repeatU = star.sampler.repeatV = i < 2;
            }
            w.starsTexture      = &result->celestialImages[0];
            w.starsNoiseTexture = &result->celestialImages[1];
            if (lunar) {
                w.moonColorTexture  = &result->celestialImages[2];
                w.moonNormalTexture = &result->celestialImages[3];
                if (w.moonColorTexture->format != ShaderImageFormat::RGBA16Float ||
                    w.moonNormalTexture->format != ShaderImageFormat::RGBA16Float)
                    throw std::runtime_error("Lunar runtime samples require linear RGBA16F");
            }
            w.daylightEnabled = true;
        }
        w.corners        = result->corners;
        w.densityTexture = &image;
        w.color          = vector<3>(field(material, "color"));
        w.gradient       = vector<4>(field(material, "gradient"));
        w.movement       = vector<2>(field(material, "movement"));
        w.moonForward    = vector<3>(field(material, "moonForward"));
        w.domeScale      = number(field(material, "domeScale"));
        w.morphRate      = number(field(material, "morphRate"));
        w.morphPeriod    = number(field(material, "morphPeriod"));
        w.morphAmount    = number(field(material, "morphAmount"));
        w.opacity        = number(field(material, "opacity"));
        w.litIntensity   = number(field(material, "litIntensity"));
        w.cloudTime      = number(field(material, "cloudTime"));
        w.moonGradient   = boolean(field(material, "moonGradient"));
        w.staticClouds   = boolean(field(material, "staticClouds"));
        return Result<SkyWispsAsset>::success(SkyWispsAsset(std::move(result)));
    } catch (const std::exception& e) {
        return Result<SkyWispsAsset>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, e.what(), "sky.wisps-asset"));
    }
}
Result<void> SkyDaylightLayer::validate() const {
    const auto failure = [] {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid daylight material coefficients", "sky.daylight"));
    };
    for (const auto* v : {&sun, &moon, &dayTint, &duskTint, &nightTint, &glow, &controls, &stars, &twinkle})
        for (float x : *v)
            if (!std::isfinite(x) || x < 0 || x > 1e6f) return failure();
    for (const auto* v : {&moonOrbit, &starUv})
        for (float x : *v)
            if (!std::isfinite(x) || std::abs(x) > 1e6f) return failure();
    if (std::abs(moonOrbit[0]) > 90 || std::abs(moonOrbit[1]) > 360 || std::abs(moonOrbit[2]) >= 1 ||
        std::abs(moonOrbit[3]) > 1 || controls[2] > 1 || starUv[0] <= 0 || twinkle[3] != 0)
        return failure();
    for (size_t i = 0; i < scatteringCurve.size(); ++i) {
        const auto& k = scatteringCurve[i];
        for (float x : k)
            if (!std::isfinite(x) || x < 0 || x > 1e6f) return failure();
        if (k[0] > 180 || (i && k[0] <= scatteringCurve[i - 1][0])) return failure();
    }
    for (const auto* v : {&rayDay, &rayDusk, &rayNight, &absorptionDay, &absorptionNight})
        for (float x : *v)
            if (!std::isfinite(x) || x < 0 || x > 1) return failure();
    if (rayDusk[3] != 0 || rayNight[3] != 0) return failure();
    for (const auto* v : {&moonDiskColor, &moonDiskShape, &moonDiskLighting, &moonDiskGlow})
        for (float x : *v)
            if (!std::isfinite(x) || std::abs(x) > 1e6f) return failure();
    for (size_t i = 0; i < 3; ++i)
        if (moonDiskColor[i] < 0) return failure();
    if ((moonDiskColor[3] != 0 && moonDiskColor[3] != 1) || moonDiskShape[0] <= 0 || moonDiskShape[0] > 1 ||
        moonDiskShape[1] <= 0 || moonDiskShape[1] > 10 || std::abs(moonDiskShape[2]) > 360 || moonDiskShape[3] < 0 ||
        moonDiskShape[3] > 1 || moonDiskLighting[0] < 0 || moonDiskLighting[1] < 0 || moonDiskLighting[2] < 0 ||
        moonDiskLighting[2] > 29.53f || moonDiskLighting[3] < 0 || moonDiskLighting[3] > 10 || moonDiskGlow[0] < 0 ||
        moonDiskGlow[1] <= 0 || moonDiskGlow[2] != 0 || moonDiskGlow[3] != 0)
        return failure();
    return Result<void>::success();
}
}  // namespace eve::graphics
