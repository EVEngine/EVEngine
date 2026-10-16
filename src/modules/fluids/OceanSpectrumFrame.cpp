#include "fluids/OceanFourierInternal.h"
#include "fluids/OceanInterpolationInternal.h"
#include "fluids/OceanSpectrumInternal.h"

#include <array>
#include <cmath>
#include <numbers>

namespace eve::fluids {
namespace {
using Complex        = std::complex<double>;
constexpr double tau = 2.0 * std::numbers::pi;

bool validPoint(glm::dvec2 q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::abs(q.x) <= 1e9 && std::abs(q.y) <= 1e9;
}
template <class T>
Result<T> invalid() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean frame query", "fluids.ocean.frame"));
}
struct Evaluation {
    glm::dvec3 displacement{0.0};
    glm::dvec3 velocity{0.0};
    glm::dvec3 dx{0.0};
    glm::dvec3 dz{0.0};
};
Evaluation evaluate(std::span<const OceanSpectrumGrid> grids, glm::dvec2 q) {
    Evaluation out;
    for (const auto& g : grids) {
        const double scale = g.resolution / g.length;
        glm::dvec2   uv    = q / g.length;
        uv                 = (uv - glm::floor(uv)) * static_cast<double>(g.resolution);
        const auto   baseX = static_cast<unsigned>(uv.x), baseZ = static_cast<unsigned>(uv.y);
        const double fx = uv.x - baseX, fz = uv.y - baseZ;
        const auto   wx = ocean_detail::weights(fx), wz = ocean_detail::weights(fz), dx = ocean_detail::derivatives(fx),
                   dz = ocean_detail::derivatives(fz);
        for (unsigned j = 0; j < 4; ++j) {
            for (unsigned i = 0; i < 4; ++i) {
                const auto x     = (baseX + g.resolution + i - 1) % g.resolution;
                const auto z     = (baseZ + g.resolution + j - 1) % g.resolution;
                const auto index = z * g.resolution + x;
                out.displacement += g.displacement[index] * (wx[i] * wz[j]);
                out.velocity += g.velocity[index] * (wx[i] * wz[j]);
                out.dx += g.displacement[index] * (dx[i] * wz[j] * scale);
                out.dz += g.displacement[index] * (wx[i] * dz[j] * scale);
            }
        }
    }
    return out;
}
}  // namespace

Result<OceanSpectrumFrame> OceanSpectrumFrameBuilder::build() const {
    if (!std::isfinite(time_) || std::abs(time_) > 1e9) return invalid<OceanSpectrumFrame>();
    OceanSpectrumFrame out;
    out.time_     = time_;
    out.seaLevel_ = data_->settings.seaLevel;
    out.current_  = data_->settings.current;
    for (const auto& band : data_->bands) {
        const auto                          n = band.settings.resolution;
        std::array<std::vector<Complex>, 6> fields;
        for (auto& field : fields) field.resize(n * n);
        for (unsigned z = 0; z < n; ++z) {
            for (unsigned x = 0; x < n; ++x) {
                const int ix = x < n / 2 ? static_cast<int>(x) : static_cast<int>(x) - static_cast<int>(n);
                const int iz = z < n / 2 ? static_cast<int>(z) : static_cast<int>(z) - static_cast<int>(n);
                if (ix == 0 && iz == 0) continue;
                const glm::dvec2 kvec = glm::dvec2(ix, iz) * (tau / band.settings.length);
                const double     k = glm::length(kvec), omega = std::sqrt(data_->settings.gravity * k);
                const auto       index    = z * n + x;
                const auto       opposite = ((n - z) % n) * n + (n - x) % n;
                const auto forward = band.coefficients[index] * std::polar(1.0, std::remainder(-omega * time_, tau));
                const auto backward =
                    std::conj(band.coefficients[opposite]) * std::polar(1.0, std::remainder(omega * time_, tau));
                const auto advection = std::polar(1.0, std::remainder(-glm::dot(kvec, out.current_) * time_, tau));
                const auto height    = (forward + backward) * advection;
                const auto velocity  = Complex(0.0, omega) * (backward - forward) * advection;
                const auto dx        = Complex(0.0, kvec.x / k * data_->settings.choppiness);
                const auto dz        = Complex(0.0, kvec.y / k * data_->settings.choppiness);
                fields[0][index]     = dx * height;
                fields[1][index]     = height;
                fields[2][index]     = dz * height;
                fields[3][index]     = dx * velocity;
                fields[4][index]     = velocity;
                fields[5][index]     = dz * velocity;
            }
        }
        for (auto& field : fields) ocean_detail::inverse2D(field, n);
        OceanSpectrumGrid grid;
        grid.resolution = n;
        grid.length     = band.settings.length;
        grid.displacement.resize(n * n);
        grid.velocity.resize(n * n);
        for (unsigned i = 0; i < n * n; ++i) {
            grid.displacement[i] = {fields[0][i].real(), fields[1][i].real(), fields[2][i].real()};
            grid.velocity[i]     = {fields[3][i].real(), fields[4][i].real(), fields[5][i].real()};
        }
        out.grids_.push_back(std::move(grid));
    }
    return Result<OceanSpectrumFrame>::success(std::move(out));
}

Result<glm::dvec3> OceanSpectrumFrame::position(glm::dvec2 parameterXZ) const {
    if (!validPoint(parameterXZ)) return invalid<glm::dvec3>();
    return Result<glm::dvec3>::success(glm::dvec3(parameterXZ.x, seaLevel_, parameterXZ.y) +
                                       evaluate(grids_, parameterXZ).displacement);
}

Result<OceanSurfaceGeometry> OceanSpectrumFrame::geometry(glm::dvec2 parameterXZ) const {
    if (!validPoint(parameterXZ)) return invalid<OceanSurfaceGeometry>();
    const auto           e = evaluate(grids_, parameterXZ);
    OceanSurfaceGeometry geometry;
    geometry.position = glm::dvec3(parameterXZ.x, seaLevel_, parameterXZ.y) + e.displacement;
    geometry.tangentX += e.dx;
    geometry.tangentZ += e.dz;
    return Result<OceanSurfaceGeometry>::success(geometry);
}

Result<OceanSurfaceSample> OceanSpectrumFrame::sample(glm::dvec2 worldXZ) const {
    if (!validPoint(worldXZ)) return invalid<OceanSurfaceSample>();
    auto q = worldXZ;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const auto   e        = evaluate(grids_, q);
        const auto   residual = q + glm::dvec2(e.displacement.x, e.displacement.z) - worldXZ;
        const double a = 1.0 + e.dx.x, b = e.dz.x, c = e.dx.z, d = 1.0 + e.dz.z;
        const double det = a * d - b * c;
        if (det <= 1e-6 || !std::isfinite(det)) break;
        if (glm::length(residual) < 1e-6) {
            OceanSurfaceSample sample;
            sample.height             = seaLevel_ + e.displacement.y;
            sample.normal             = glm::normalize(glm::cross(glm::dvec3(b, e.dz.y, d), glm::dvec3(a, e.dx.y, c)));
            sample.velocity           = e.velocity + glm::dvec3(current_.x, 0.0, current_.y);
            sample.time               = time_;
            sample.parameterPosition  = q;
            sample.horizontalJacobian = det;
            return Result<OceanSurfaceSample>::success(sample);
        }
        auto         step   = glm::dvec2(d * residual.x - b * residual.y, a * residual.y - c * residual.x) / det;
        const double length = glm::length(step);
        if (length > 10.0) step *= 10.0 / length;
        q -= step;
        if (!validPoint(q)) break;
    }
    return Result<OceanSurfaceSample>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "Ocean surface folds or world-space inversion did not converge",
                          "fluids.ocean.sample"));
}
}  // namespace eve::fluids
