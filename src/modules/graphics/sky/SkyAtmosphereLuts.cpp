#include "graphics/sky/SkyAtmosphereLuts.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include "graphics/sky/SkyAtmospherePass.h"

namespace eve::graphics {
namespace {
using Vec           = glm::dvec3;
constexpr double Pi = 3.14159265358979323846;
Vec              vector(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
bool             range(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool             coefficients(const std::array<float, 3>& values, float maximum = 100) {
    return std::all_of(values.begin(), values.end(), [maximum](float v) { return range(v, 0, maximum); });
}
struct Medium {
    Vec scattering, extinction;
};
Medium medium(const SkyAtmosphereParameters& p, Vec point) {
    double altitude = std::max(0.0, glm::length(point) - p.groundRadiusKm);
    Vec    ray      = vector(p.rayleigh) * std::exp(-altitude / p.rayleighHeightKm);
    double density  = std::exp(-altitude / p.mieHeightKm);
    Vec    mie      = vector(p.mieScattering) * density;
    double ozone    = std::max(0.0, 1 - std::abs(altitude - p.ozoneCenterKm) / p.ozoneHalfWidthKm);
    return {ray + mie, ray + mie + vector(p.mieAbsorption) * density + vector(p.ozoneAbsorption) * ozone};
}
glm::dvec2 sphere(Vec point, Vec direction, double radius) {
    double b = glm::dot(point, direction), d = b * b - glm::dot(point, point) + radius * radius;
    if (d < 0) return {-1, -1};
    double root = std::sqrt(d);
    return {-b - root, -b + root};
}
glm::dvec2 transmissionUv(const SkyAtmosphereParameters& p, double height, double cosine) {
    double bottom = p.groundRadiusKm, top = bottom + p.atmosphereHeightKm;
    double h   = std::sqrt(top * top - bottom * bottom),
           rho = std::sqrt(std::max(0.0, height * height - bottom * bottom));
    double distance =
        std::max(0.0, -height * cosine + std::sqrt(std::max(0.0, height * height * (cosine * cosine - 1) + top * top)));
    double minimum = top - height, maximum = rho + h;
    return glm::clamp(glm::dvec2((distance - minimum) / (maximum - minimum), rho / h), glm::dvec2(0), glm::dvec2(1));
}
Vec sample(const std::vector<float>& image, unsigned width, unsigned height, glm::dvec2 uv) {
    auto     p = glm::clamp(uv * glm::dvec2(width, height) - .5, glm::dvec2(0), glm::dvec2(width - 1, height - 1));
    unsigned x = unsigned(p.x), y = unsigned(p.y), x1 = std::min(x + 1, width - 1), y1 = std::min(y + 1, height - 1);
    auto     at = [&](unsigned a, unsigned b) {
        size_t i = (size_t(b) * width + a) * 4;
        return Vec(image[i], image[i + 1], image[i + 2]);
    };
    return glm::mix(glm::mix(at(x, y), at(x1, y), p.x - x), glm::mix(at(x, y1), at(x1, y1), p.x - x), p.y - y);
}
void store(std::vector<float>& image, size_t texel, Vec value) {
    for (int c = 0; c < 3; ++c) {
        // Match the reference R11G11B10 LUT storage before bilinear filtering.
        // Keeping the decoded values in RGBA32F preserves the shared volume ABI.
        const double maximum  = c == 2 ? 64512.0 : 65024.0;
        const double v        = std::clamp(value[c], 0.0, maximum);
        const double exponent = std::max(-14.0, std::floor(std::log2(std::max(v, 1e-30))));
        const double quantum  = std::exp2(exponent - (c == 2 ? 5 : 6));
        const double scaled = v / quantum, lower = std::floor(scaled);
        const double fraction        = scaled - lower;
        const double rounded         = lower + (fraction > .5 || (fraction == .5 && std::fmod(lower, 2.0) != 0));
        image[texel * 4 + size_t(c)] = float(rounded * quantum);
    }
    image[texel * 4 + 3] = 1;
}
struct Integrated {
    Vec luminance{0}, feedback{0};
};
Integrated integrate(const SkyAtmosphereParameters& p, const detail::SkyAtmosphereLuts& tables, Vec origin,
                     Vec direction, Vec light) {
    double top        = double(p.groundRadiusKm) + p.atmosphereHeightKm;
    auto   ground     = sphere(origin, direction, p.groundRadiusKm);
    double end        = sphere(origin, direction, top).y;
    bool   hitsGround = ground.x > 0 && ground.x < end;
    if (hitsGround) end = ground.x;
    double     step = end / 15;
    Integrated result;
    Vec        throughput(1);
    auto       transmission = [&](Vec point) {
        double h = glm::length(point);
        return sample(tables.transmittance, tables.TransmittanceWidth, tables.TransmittanceHeight,
                            transmissionUv(p, h, glm::dot(point, light) / h));
    };
    for (int i = 0; i < 15; ++i) {
        Vec  point   = origin + direction * ((i + .3) * step);
        auto m       = medium(p, point);
        Vec  segment = glm::exp(-m.extinction * step);
        bool shadow  = sphere(point, light, p.groundRadiusKm).x > 0;
        Vec  source  = shadow ? Vec(0) : transmission(point) * m.scattering / (4 * Pi);
        result.luminance += throughput * source * (Vec(1) - segment) / glm::max(m.extinction, Vec(1e-9));
        // UE's finite-order approximation uses a rectangle integral for feedback.
        result.feedback += throughput * m.scattering * step;
        throughput *= segment;
    }
    if (hitsGround) {
        Vec    point  = origin + direction * end;
        double cosine = std::max(0.0, glm::dot(glm::normalize(point), light));
        result.luminance += transmission(point) * throughput * cosine * vector(p.groundAlbedo) / Pi;
    }
    return result;
}
}  // namespace

Result<void> SkyAtmosphereParameters::validate() const {
    auto fog = heightFog.validate();
    if (!fog) return fog;
    if (!coefficients(rayleigh) || !coefficients(mieScattering) || !coefficients(mieAbsorption) ||
        !coefficients(ozoneAbsorption) || !coefficients(groundAlbedo, 1) || !range(rayleighHeightKm, 1e-5f, 1e6f) ||
        !range(mieHeightKm, 1e-5f, 1e6f) || !range(ozoneHalfWidthKm, 1e-5f, 1e6f) || !range(ozoneCenterKm, 0, 1e6f) ||
        !range(groundRadiusKm, 1, 1e6f) || !range(atmosphereHeightKm, .001f, 1e4f) ||
        groundRadiusKm + atmosphereHeightKm <= groundRadiusKm || !range(mieAnisotropy, -.9999f, .9999f) ||
        !range(multiScatteringFactor, 0, 100) || (heightFog.densityPerMetre > 0 && atmosphereHeightKm <= 6))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid atmospheric coefficient, geometry or energy control",
                                                       "sky.atmosphere"));
    return Result<void>::success();
}

Result<void> SkyHeightFogParameters::validate() const {
    if (!range(directionalElevationRange[0], -1, 1) || !range(directionalElevationRange[1], -1, 1) ||
        directionalElevationRange[0] > directionalElevationRange[1] || !range(densityPerMetre, 0, 1) ||
        !range(heightFalloffPerMetre, 0, 1) || !range(baseHeightMetres, -1e6f, 1e6f) ||
        !range(startDistanceMetres, 0, 1e6f) || !range(maximumOpacity, 0, 1) || !coefficients(inscattering) ||
        !coefficients(directionalInscattering) || !range(directionalExponent, .001f, 100) ||
        !range(directionalStartDistanceMetres, 0, 1e6f) || !range(atmosphereContribution, 0, 100) ||
        !range(skyDistanceMetres, 1, 1e7f) || startDistanceMetres >= skyDistanceMetres)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid height fog density, color, height or distance",
                                                       "sky.height-fog"));
    return Result<void>::success();
}

Result<detail::SkyAtmosphereLuts> detail::bakeSkyAtmosphereLuts(const SkyAtmosphereParameters& p) {
    using Baked = Result<SkyAtmosphereLuts>;
    auto valid  = p.validate();
    if (!valid) return Baked::failure(valid.status());
    try {
        SkyAtmosphereLuts tables;
        tables.transmittance.resize(size_t(tables.TransmittanceWidth) * tables.TransmittanceHeight * 4);
        tables.multiScattering.resize(size_t(tables.MultiWidth) * tables.MultiHeight * 4);
        double bottom = p.groundRadiusKm, top = bottom + p.atmosphereHeightKm,
               h = std::sqrt(top * top - bottom * bottom);
        for (unsigned y = 0; y < tables.TransmittanceHeight; ++y)
            for (unsigned x = 0; x < tables.TransmittanceWidth; ++x) {
                double rho = h * (y + .5) / tables.TransmittanceHeight, height = std::sqrt(rho * rho + bottom * bottom);
                double minimum  = top - height,
                       distance = minimum + (x + .5) / tables.TransmittanceWidth * (rho + h - minimum);
                double cosine =
                    std::clamp((h * h - rho * rho - distance * distance) / (2 * height * distance), -1.0, 1.0);
                Vec    origin(0, height, 0), direction(std::sqrt(std::max(0.0, 1 - cosine * cosine)), cosine, 0);
                double step = distance / 10;
                Vec    depth(0);
                for (int i = 0; i < 10; ++i)
                    depth += medium(p, origin + direction * ((i + .3) * step)).extinction * step;
                store(tables.transmittance, size_t(y) * tables.TransmittanceWidth + x, glm::exp(-depth));
            }
        for (unsigned y = 0; y < tables.MultiHeight; ++y)
            for (unsigned x = 0; x < tables.MultiWidth; ++x) {
                double cosine = 2 * (x + .5) / tables.MultiWidth - 1;
                Vec    light(std::sqrt(std::max(0.0, 1 - cosine * cosine)), cosine, 0);
                Vec    origin(0, bottom + (y + .5) / tables.MultiHeight * (top - bottom), 0);
                auto   up = integrate(p, tables, origin, Vec(0, 1, 0), light),
                     down = integrate(p, tables, origin, Vec(0, -1, 0), light);
                Vec r = (up.feedback + down.feedback) * .5, r2 = r * r;
                Vec luminance = (up.luminance + down.luminance) * .5 * (Vec(1) + r + r2 + r2 * r + r2 * r2) *
                                double(p.multiScatteringFactor);
                if (!std::isfinite(luminance.x) || !std::isfinite(luminance.y) || !std::isfinite(luminance.z) ||
                    glm::any(glm::greaterThan(luminance, Vec(std::numeric_limits<float>::max()))))
                    return Baked::failure(
                        Diagnostic::error(DiagnosticCode::Failed, "Nonfinite atmospheric lookup table"));
                store(tables.multiScattering, size_t(y) * tables.MultiWidth + x, luminance);
            }
        return Baked::success(std::move(tables));
    } catch (const std::exception& error) {
        return Baked::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "sky.atmosphere"));
    }
}

Result<std::array<float, 3>> detail::bakeDistantSkyAmbient(const SkyAtmosphereParameters& p,
                                                           const SkyAtmosphereLuts&       tables,
                                                           const std::array<float, 3>&    lightDirection) {
    using Ambient = Result<std::array<float, 3>>;
    auto valid    = p.validate();
    if (!valid) return Ambient::failure(valid.status());
    Vec          light       = vector(lightDirection);
    const double lightLength = glm::length(light);
    const auto   finite      = [](const std::vector<float>& values) {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v) && v >= 0; });
    };
    if (!std::isfinite(lightLength) || lightLength < 1e-6 || p.atmosphereHeightKm <= 6 ||
        tables.transmittance.size() != size_t(tables.TransmittanceWidth) * tables.TransmittanceHeight * 4 ||
        tables.multiScattering.size() != size_t(tables.MultiWidth) * tables.MultiHeight * 4 ||
        !finite(tables.transmittance) || !finite(tables.multiScattering))
        return Ambient::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Distant sky requires finite matching tables, a nonzero light "
                                                  "direction and atmosphere above six kilometres",
                                                  "sky.atmosphere"));
    light /= lightLength;
    const double bottom = p.groundRadiusKm, top = bottom + p.atmosphereHeightKm;
    const Vec    origin(0, bottom + 6, 0);
    // Local fixed stream reproduces the reference's stratification without consuming simulation RNG.
    uint32_t   seed     = 0xde4dc0deu;
    const auto fraction = [&seed]() {
        seed = seed * 196314165u + 907633515u;
        return double(seed >> 9) / 8388608.0;
    };
    Vec sum(0);
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned j = 0; j < 8; ++j) {
            const double u = (i + fraction()) / 8, v = (j + fraction()) / 8;
            const double y = 1 - 2 * u, radius = std::sqrt(std::max(0.0, 1 - y * y));
            const Vec    direction(radius * std::cos(2 * Pi * v), y, radius * std::sin(2 * Pi * v));
            double       end    = sphere(origin, direction, top).y;
            const auto   ground = sphere(origin, direction, bottom);
            if (ground.x > 0) end = std::min(end, ground.x);
            const double step = end / 10;
            Vec          throughput(1), radiance(0);
            for (int k = 0; k < 10; ++k) {
                const Vec    point  = origin + direction * ((k + .3) * step);
                const double height = glm::length(point), cosine = glm::dot(point, light) / height;
                const auto   m        = medium(p, point);
                const Vec    segment  = glm::exp(-m.extinction * step);
                const Vec    sunlight = sphere(point, light, bottom).x > 0
                                            ? Vec(0)
                                            : sample(tables.transmittance, tables.TransmittanceWidth,
                                                     tables.TransmittanceHeight, transmissionUv(p, height, cosine));
                const Vec    multiple = sample(tables.multiScattering, tables.MultiWidth, tables.MultiHeight,
                                               glm::clamp(glm::dvec2(cosine * .5 + .5, (height - bottom) / (top - bottom)),
                                                          glm::dvec2(0), glm::dvec2(1)));
                const Vec    source   = m.scattering * (sunlight / (4 * Pi) + multiple);
                radiance += throughput * source * (Vec(1) - segment) / glm::max(m.extinction, Vec(1e-9));
                throughput *= segment;
            }
            sum += radiance;
        }
    }
    const Vec            average = sum / 64.0;
    std::array<float, 3> result{};
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(average[axis]) || average[axis] < 0 || average[axis] > std::numeric_limits<float>::max())
            return Ambient::failure(Diagnostic::error(DiagnosticCode::Failed, "Nonfinite distant sky ambient"));
        result[size_t(axis)] = static_cast<float>(average[axis]);
    }
    return Ambient::success(result);
}
}  // namespace eve::graphics
