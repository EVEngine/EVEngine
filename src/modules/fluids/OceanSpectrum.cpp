#include "fluids/OceanSpectrumInternal.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eve::fluids {
namespace {
constexpr double tau = 2.0 * std::numbers::pi;
double           uniform(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z      = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z      = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= z >> 31;
    return (static_cast<double>(z >> 11) + 0.5) / 9007199254740992.0;
}
bool                  finite(glm::dvec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
Result<OceanSpectrum> invalid() {
    return Result<OceanSpectrum>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean spectrum settings", "fluids.ocean.spectrum"));
}
}  // namespace

Result<OceanSpectrum> OceanSpectrumBuilder::build() const {
    const auto& s = settings;
    if (!std::isfinite(s.significantWaveHeight) || s.significantWaveHeight < 0.0 || s.significantWaveHeight > 30.0 ||
        !std::isfinite(s.peakPeriod) || s.peakPeriod < 1.0 || s.peakPeriod > 30.0 ||
        !std::isfinite(s.peakEnhancement) || s.peakEnhancement < 1.0 || s.peakEnhancement > 10.0 ||
        !std::isfinite(s.gravity) || s.gravity <= 0.0 || s.gravity > 100.0 || !std::isfinite(s.seaLevel) ||
        std::abs(s.seaLevel) > 1e6 || !std::isfinite(s.choppiness) || s.choppiness < 0.0 || s.choppiness > 1.0 ||
        !finite(s.windDirection) || glm::length(s.windDirection) < 1e-8 || glm::length(s.windDirection) > 1e6 ||
        !finite(s.current) || glm::length(s.current) > 100.0 || s.bands.empty() || s.bands.size() > 4)
        return invalid();
    double previous = 0.0;
    for (const auto& b : s.bands) {
        if (b.resolution < 8 || b.resolution > 512 || (b.resolution & (b.resolution - 1)) != 0 ||
            !std::isfinite(b.length) || b.length < 1.0 || b.length > 1e6 || !std::isfinite(b.minimumWavelength) ||
            !std::isfinite(b.maximumWavelength) || b.minimumWavelength < 2.0 * b.length / b.resolution ||
            b.maximumWavelength > b.length || b.minimumWavelength >= b.maximumWavelength ||
            b.minimumWavelength < previous)
            return invalid();
        previous = b.maximumWavelength;
    }
    auto data            = std::make_shared<OceanSpectrumData>();
    data->settings       = s;
    const auto    wind   = glm::normalize(s.windDirection);
    std::uint64_t random = s.seed;
    double        energy = 0.0;
    const double  peak   = tau / s.peakPeriod;
    for (const auto& b : s.bands) {
        OceanSpectrumData::Band band{b, {}};
        band.coefficients.resize(b.resolution * b.resolution);
        const double dk = tau / b.length;
        for (unsigned z = 0; z < b.resolution; ++z) {
            for (unsigned x = 0; x < b.resolution; ++x) {
                const int ix =
                    x < b.resolution / 2 ? static_cast<int>(x) : static_cast<int>(x) - static_cast<int>(b.resolution);
                const int iz =
                    z < b.resolution / 2 ? static_cast<int>(z) : static_cast<int>(z) - static_cast<int>(b.resolution);
                // Exclude Nyquist axes so odd displacement multipliers remain Hermitian.
                if ((ix == 0 && iz == 0) || x == b.resolution / 2 || z == b.resolution / 2) continue;
                const glm::dvec2 kvec{ix * dk, iz * dk};
                const double     k = glm::length(kvec), wavelength = tau / k;
                if (wavelength < b.minimumWavelength || wavelength >= b.maximumWavelength) continue;
                const double omega     = std::sqrt(s.gravity * k);
                const double sigma     = omega <= peak ? 0.07 : 0.09;
                const double r         = std::exp(-0.5 * std::pow((omega - peak) / (sigma * peak), 2.0));
                const double direction = std::pow(std::max(glm::dot(kvec / k, wind), 0.0), 4.0);
                const double density   = std::pow(omega, -5.0) * std::exp(-1.25 * std::pow(peak / omega, 4.0)) *
                                       std::pow(s.peakEnhancement, r) * direction * (s.gravity / (2.0 * omega)) / k;
                const double magnitude                  = std::sqrt(density * dk * dk * -std::log(uniform(random)));
                const auto   coefficient                = std::polar(magnitude, tau * uniform(random));
                band.coefficients[z * b.resolution + x] = coefficient;
                energy += 2.0 * std::norm(coefficient);
            }
        }
        data->bands.push_back(std::move(band));
    }
    if (s.significantWaveHeight > 0.0 && energy < 1e-30) return invalid();
    const double variance = s.significantWaveHeight * s.significantWaveHeight / 16.0;
    const double scale    = energy > 0.0 ? std::sqrt(variance / energy) : 0.0;
    for (auto& band : data->bands)
        for (auto& coefficient : band.coefficients) coefficient *= scale;
    data->variance = variance;
    return Result<OceanSpectrum>::success(OceanSpectrum(std::move(data)));
}

OceanSpectrumFrameBuilder OceanSpectrum::frameBuilder(double time) const { return {data_, time}; }
double                    OceanSpectrum::variance() const { return data_->variance; }
}  // namespace eve::fluids
