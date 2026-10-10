#include "daynight/sky/SkyProfile.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include "common/Value.h"
#include "daynight/sky/SkyRuntime.h"
#include "daynight/sky/SkySolarOrbit.h"
#include "graphics/sky/SkyAtmospherePass.h"

namespace eve::daynight {
namespace {
void fields(const Value& value, std::initializer_list<std::string_view> names) {
    if (!value.isObject()) throw std::runtime_error("Expected sky profile object");
    const auto keys = value.keys();
    if (keys.size() != names.size()) throw std::runtime_error("Missing or extra sky profile fields");
    for (const auto& key : keys)
        if (std::find(names.begin(), names.end(), key) == names.end())
            throw std::runtime_error("Unknown sky profile field: " + key);
}
const Value& field(const Value& value, const char* name) {
    const auto* result = value.find(name);
    if (!result) throw std::runtime_error(std::string("Missing sky profile field: ") + name);
    return *result;
}
double number(const Value& value) {
    if (!value.isNumeric()) throw std::runtime_error("Sky profile requires a number");
    const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) throw std::runtime_error("Nonfinite sky profile number");
    return result;
}
double number(const Value& value, const char* name) { return number(field(value, name)); }
float  scalar(const Value& value, const char* name) {
    const double result = number(value, name);
    if (std::abs(result) > std::numeric_limits<float>::max()) throw std::runtime_error("Sky profile float overflow");
    const float converted = static_cast<float>(result);
    if (result != 0 && converted == 0) throw std::runtime_error("Sky profile float underflow");
    return converted;
}
template <typename T, size_t N>
std::array<T, N> vector(const Value& object, const char* name) {
    const auto& value = field(object, name);
    if (!value.isArray() || value.arraySize() != N) throw std::runtime_error("Invalid sky profile vector size");
    std::array<T, N> result{};
    for (size_t i = 0; i < N; ++i) {
        const double item = number(value.at(i));
        if (std::abs(item) > std::numeric_limits<T>::max()) throw std::runtime_error("Sky profile vector overflow");
        result[i] = static_cast<T>(item);
        if (item != 0 && result[i] == 0) throw std::runtime_error("Sky profile vector underflow");
    }
    return result;
}
void text(const Value& object, const char* name, std::string_view expected) {
    const auto& value = field(object, name);
    if (!value.isString() || value.asString() != expected)
        throw std::runtime_error(std::string("Unsupported sky profile ") + name);
}
}  // namespace
struct SkyProfile::Impl {
    SkyRuntimeSettings                settings;
    graphics::SkyAtmosphereParameters atmosphere;
    std::string                       wispsAsset;
    std::optional<uint32_t>           seed;
};
SkyProfile::SkyProfile(std::shared_ptr<const Impl> data) : data_(std::move(data)) {}
const SkyRuntimeSettings&                SkyProfile::settings() const noexcept { return data_->settings; }
const graphics::SkyAtmosphereParameters& SkyProfile::atmosphere() const noexcept { return data_->atmosphere; }
const std::string&                       SkyProfile::wispsAsset() const noexcept { return data_->wispsAsset; }

std::optional<uint32_t> SkyProfile::proceduralSeed() const noexcept { return data_->seed; }
Result<SkyProfile>      SkyProfile::decode(const Value& document) {
    try {
        const auto& version = field(document, "version");
        if (!version.isInt64() || version.asInt() < 1 || version.asInt() > 6)
            throw std::runtime_error("Unsupported sky profile version");
        if (version.asInt() == 1)
            fields(document, {"schema", "version", "mode", "clock", "weather", "sun", "atmosphere"});
        else if (version.asInt() == 2)
            fields(document, {"schema", "version", "mode", "clock", "weather", "sun", "atmosphere", "heightFog"});
        else if (version.asInt() == 3)
            fields(document,
                        {"schema", "version", "mode", "clock", "weather", "sun", "atmosphere", "heightFog", "wispsAsset"});
        else if (version.asInt() == 6)
            fields(document, {"schema", "version", "mode", "clock", "weather", "sun", "atmosphere", "heightFog",
                                   "wispsAsset", "cloudMotion", "proceduralSeed"});
        else
            fields(document, {"schema", "version", "mode", "clock", "weather", "sun", "atmosphere", "heightFog",
                                   "wispsAsset", "cloudMotion"});
        text(document, "schema", "eve.sky-profile");
        text(document, "mode", "atmosphere");
        auto result = std::make_shared<Impl>();
        if (version.asInt() >= 3) {
            const auto& path = field(document, "wispsAsset");
            if (version.asInt() == 6) {
                const auto& seed = field(document, "proceduralSeed");
                if (!seed.isInt64() || seed.asInt() < 0 || seed.asInt() > 4294967295LL)
                    throw std::runtime_error("Invalid procedural sky seed");
                if (path.isNull()) result->seed = static_cast<uint32_t>(seed.asInt());
            }
            if (!result->seed && (!path.isString() || path.asString().empty() || path.asString().size() > 4096))
                throw std::runtime_error("Invalid wisps manifest path");
            if (!result->seed) result->wispsAsset = path.asString();
        }
        if (version.asInt() >= 4) {
            const auto& source = field(document, "cloudMotion");
            fields(source, {"phase", "speed", "timeScale", "windMultiplier"});
            result->settings.cloudMotion = {number(source, "phase"), number(source, "speed"),
                                                 number(source, "timeScale"), number(source, "windMultiplier")};
        }
        auto motionValid = result->settings.cloudMotion.validate();
        if (!motionValid) return Result<SkyProfile>::failure(motionValid.status());
        const auto& clock = field(document, "clock");
        fields(clock, {"initialHour", "hoursPerSecond"});
        result->settings.clock = {number(clock, "initialHour"), number(clock, "hoursPerSecond")};
        const auto& weather    = field(document, "weather");
        fields(weather, {"cloudCoverage", "fog", "rain", "snow", "windXZ"});
        result->settings.weather = {number(weather, "cloudCoverage"), number(weather, "fog"), number(weather, "rain"),
                                         number(weather, "snow"), vector<double, 2>(weather, "windXZ")};
        const auto& sun = field(document, "sun");
        fields(sun, {"pitchDegrees", "yawDegrees", "irradiance"});
        result->settings.sunPitchDegrees = number(sun, "pitchDegrees");
        result->settings.sunYawDegrees   = number(sun, "yawDegrees");
        result->settings.sunIrradiance   = vector<float, 3>(sun, "irradiance");
        const auto& atmosphere           = field(document, "atmosphere");
        fields(atmosphere, {"rayleigh", "rayleighHeightKm", "mieScattering", "mieHeightKm", "mieAbsorption",
                                 "mieAnisotropy", "ozoneAbsorption", "ozoneCenterKm", "ozoneHalfWidthKm", "groundRadiusKm",
                                 "atmosphereHeightKm", "groundAlbedo", "multiScatteringFactor"});
        auto& p                 = result->atmosphere;
        p.rayleigh              = vector<float, 3>(atmosphere, "rayleigh");
        p.rayleighHeightKm      = scalar(atmosphere, "rayleighHeightKm");
        p.mieScattering         = vector<float, 3>(atmosphere, "mieScattering");
        p.mieHeightKm           = scalar(atmosphere, "mieHeightKm");
        p.mieAbsorption         = vector<float, 3>(atmosphere, "mieAbsorption");
        p.mieAnisotropy         = scalar(atmosphere, "mieAnisotropy");
        p.ozoneAbsorption       = vector<float, 3>(atmosphere, "ozoneAbsorption");
        p.ozoneCenterKm         = scalar(atmosphere, "ozoneCenterKm");
        p.ozoneHalfWidthKm      = scalar(atmosphere, "ozoneHalfWidthKm");
        p.groundRadiusKm        = scalar(atmosphere, "groundRadiusKm");
        p.atmosphereHeightKm    = scalar(atmosphere, "atmosphereHeightKm");
        p.groundAlbedo          = vector<float, 3>(atmosphere, "groundAlbedo");
        p.multiScatteringFactor = scalar(atmosphere, "multiScatteringFactor");
        if (version.asInt() >= 2) {
            const auto& source = field(document, "heightFog");
            if (version.asInt() >= 5)
                fields(source, {"densityPerMetre", "heightFalloffPerMetre", "baseHeightMetres", "startDistanceMetres",
                                     "inscattering", "maximumOpacity", "directionalInscattering", "directionalExponent",
                                     "directionalStartDistanceMetres", "atmosphereContribution", "skyDistanceMetres",
                                     "directionalElevationRange"});
            else
                fields(source, {"densityPerMetre", "heightFalloffPerMetre", "baseHeightMetres", "startDistanceMetres",
                                     "inscattering", "maximumOpacity", "directionalInscattering", "directionalExponent",
                                     "directionalStartDistanceMetres", "atmosphereContribution", "skyDistanceMetres"});
            auto& fog = p.heightFog;
            if (version.asInt() >= 5)
                fog.directionalElevationRange = vector<float, 2>(source, "directionalElevationRange");
            fog.densityPerMetre                = scalar(source, "densityPerMetre");
            fog.heightFalloffPerMetre          = scalar(source, "heightFalloffPerMetre");
            fog.baseHeightMetres               = scalar(source, "baseHeightMetres");
            fog.startDistanceMetres            = scalar(source, "startDistanceMetres");
            fog.inscattering                   = vector<float, 3>(source, "inscattering");
            fog.maximumOpacity                 = scalar(source, "maximumOpacity");
            fog.directionalInscattering        = vector<float, 3>(source, "directionalInscattering");
            fog.directionalExponent            = scalar(source, "directionalExponent");
            fog.directionalStartDistanceMetres = scalar(source, "directionalStartDistanceMetres");
            fog.atmosphereContribution         = scalar(source, "atmosphereContribution");
            fog.skyDistanceMetres              = scalar(source, "skyDistanceMetres");
        }
        auto timeline = SkyTimeline::create(result->settings.clock, result->settings.weather);
        if (!timeline) return Result<SkyProfile>::failure(timeline.status());
        auto orbit = SkySolarOrbit::create(result->settings.sunPitchDegrees, result->settings.sunYawDegrees);
        if (!orbit) return Result<SkyProfile>::failure(orbit.status());
        for (float intensity : result->settings.sunIrradiance)
            if (intensity < 0) throw std::runtime_error("Solar irradiance must be nonnegative");
        auto optical = p.validate();
        if (!optical) return Result<SkyProfile>::failure(optical.status());
        return Result<SkyProfile>::success(SkyProfile(std::move(result)));
    } catch (const std::exception& error) {
        return Result<SkyProfile>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "sky.profile"));
    }
}
}  // namespace eve::daynight
