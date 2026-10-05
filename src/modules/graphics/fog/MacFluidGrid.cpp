#include "graphics/fog/MacFluidGrid.h"

#include "common/Diagnostic.h"
#include "graphics/fog/FogInteractor.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {

Result<void> MacFluidGrid::configure(const FogDensityField& field, float fixedDt) {
    if (field.width() < 2 || field.height() < 2 || field.depth() < 2) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "density field must be allocated before MAC configure",
            "field", {}, "graphics.fog"));
    }
    if (!std::isfinite(fixedDt) || fixedDt <= 0.f || fixedDt > 0.1f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fixedDt must be in (0, 0.1]", "fixedDt", {},
            "graphics.fog"));
    }

    width_ = field.width();
    height_ = field.height();
    depth_ = field.depth();
    bounds_ = field.bounds();
    cellSize_ = field.cellSize();
    fixedDt_ = fixedDt;
    accumulator_ = 0.f;
    stepCount_ = 0;

    const std::size_t cells = static_cast<std::size_t>(width_) * height_ * depth_;
    density_.assign(cells, 0.f);
    pressure_.assign(cells, 0.f);
    divergence_.assign(cells, 0.f);
    tmpDensity_.assign(cells, 0.f);
    solid_.assign(cells, 0);
    u_.assign(static_cast<std::size_t>(width_ + 1) * height_ * depth_, 0.f);
    v_.assign(static_cast<std::size_t>(width_) * (height_ + 1) * depth_, 0.f);
    w_.assign(static_cast<std::size_t>(width_) * height_ * (depth_ + 1), 0.f);
    tmpU_ = u_;
    tmpV_ = v_;
    tmpW_ = w_;
    return Result<void>::success();
}

std::size_t MacFluidGrid::cellIndex(int x, int y, int z) const noexcept {
    return (static_cast<std::size_t>(z) * height_ + y) * width_ + x;
}
std::size_t MacFluidGrid::uIndex(int i, int y, int z) const noexcept {
    return (static_cast<std::size_t>(z) * height_ + y) * (width_ + 1) + i;
}
std::size_t MacFluidGrid::vIndex(int x, int j, int z) const noexcept {
    return (static_cast<std::size_t>(z) * (height_ + 1) + j) * width_ + x;
}
std::size_t MacFluidGrid::wIndex(int x, int y, int k) const noexcept {
    return (static_cast<std::size_t>(k) * height_ + y) * width_ + x;
}

Result<void> MacFluidGrid::pullDensity(const FogDensityField& field) {
    if (field.width() != width_ || field.height() != height_ || field.depth() != depth_) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Rejected, "density field resolution mismatch", "pullDensity", {},
            "graphics.fog"));
    }
    const auto src = field.densitySpan();
    std::copy(src.begin(), src.end(), density_.begin());
    return Result<void>::success();
}

Result<void> MacFluidGrid::pushDensity(FogDensityField& field) const {
    if (field.width() != width_ || field.height() != height_ || field.depth() != depth_) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Rejected, "density field resolution mismatch", "pushDensity", {},
            "graphics.fog"));
    }
    for (int z = 0; z < depth_; ++z)
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) field.setDensity(x, y, z, densityAt(x, y, z));
    return Result<void>::success();
}

float MacFluidGrid::densityAt(int x, int y, int z) const noexcept {
    if (density_.empty()) return 0.f;
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    z = std::clamp(z, 0, depth_ - 1);
    return density_[cellIndex(x, y, z)];
}

void MacFluidGrid::setDensity(int x, int y, int z, float density) {
    if (density_.empty()) return;
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    z = std::clamp(z, 0, depth_ - 1);
    density_[cellIndex(x, y, z)] = std::max(density, 0.f);
}

float MacFluidGrid::uAt(int i, int y, int z) const noexcept {
    i = std::clamp(i, 0, width_);
    y = std::clamp(y, 0, height_ - 1);
    z = std::clamp(z, 0, depth_ - 1);
    return u_[uIndex(i, y, z)];
}
float MacFluidGrid::vAt(int x, int j, int z) const noexcept {
    x = std::clamp(x, 0, width_ - 1);
    j = std::clamp(j, 0, height_);
    z = std::clamp(z, 0, depth_ - 1);
    return v_[vIndex(x, j, z)];
}
float MacFluidGrid::wAt(int x, int y, int k) const noexcept {
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    k = std::clamp(k, 0, depth_);
    return w_[wIndex(x, y, k)];
}

glm::vec3 MacFluidGrid::velocityAtCell(int x, int y, int z) const noexcept {
    return {0.5f * (uAt(x, y, z) + uAt(x + 1, y, z)), 0.5f * (vAt(x, y, z) + vAt(x, y + 1, z)),
            0.5f * (wAt(x, y, z) + wAt(x, y, z + 1))};
}

FogCflReport MacFluidGrid::diagnoseCfl() const noexcept {
    FogCflReport report;
    report.cellSize = std::min({cellSize_.x, cellSize_.y, cellSize_.z});
    report.dt = fixedDt_;
    float maxSpeed = 0.f;
    for (float s : u_) maxSpeed = std::max(maxSpeed, std::fabs(s));
    for (float s : v_) maxSpeed = std::max(maxSpeed, std::fabs(s));
    for (float s : w_) maxSpeed = std::max(maxSpeed, std::fabs(s));
    report.maxSpeed = maxSpeed;
    report.cfl = report.cellSize > 0.f ? maxSpeed * fixedDt_ / report.cellSize : 0.f;
    report.stable = report.cfl <= 1.f;
    return report;
}

glm::vec3 MacFluidGrid::sampleVelocity(const glm::vec3& localCell) const noexcept {
    const float x = std::clamp(localCell.x, 0.f, static_cast<float>(width_ - 1));
    const float y = std::clamp(localCell.y, 0.f, static_cast<float>(height_ - 1));
    const float z = std::clamp(localCell.z, 0.f, static_cast<float>(depth_ - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    return velocityAtCell(x0, y0, z0);
}

float MacFluidGrid::sampleDensityLocal(const glm::vec3& localCell) const noexcept {
    const float x = std::clamp(localCell.x, 0.f, static_cast<float>(width_ - 1));
    const float y = std::clamp(localCell.y, 0.f, static_cast<float>(height_ - 1));
    const float z = std::clamp(localCell.z, 0.f, static_cast<float>(depth_ - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const int z1 = std::min(z0 + 1, depth_ - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float fz = z - static_cast<float>(z0);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = lerp(densityAt(x0, y0, z0), densityAt(x1, y0, z0), fx);
    const float c10 = lerp(densityAt(x0, y1, z0), densityAt(x1, y1, z0), fx);
    const float c01 = lerp(densityAt(x0, y0, z1), densityAt(x1, y0, z1), fx);
    const float c11 = lerp(densityAt(x0, y1, z1), densityAt(x1, y1, z1), fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

glm::vec3 MacFluidGrid::traceBounded(const glm::vec3& start, const glm::vec3& delta,
                                     const FogInteractor* interactor) const noexcept {
    if (interactor) return interactor->clipAdvection(start, delta, bounds_, cellSize_);
    return start + delta;
}

void MacFluidGrid::applyForcesAndWind(const SceneWind& wind, float dt, float simTime) {
    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int i = 0; i <= width_; ++i) {
                if (solid_.size() && ((i > 0 && solid_[cellIndex(i - 1, y, z)]) ||
                                      (i < width_ && solid_[cellIndex(i, y, z)])))
                    continue;
                const glm::vec3 world =
                    bounds_.minimum + glm::vec3(static_cast<float>(i) * cellSize_.x,
                                                (y + 0.5f) * cellSize_.y, (z + 0.5f) * cellSize_.z);
                u_[uIndex(i, y, z)] += wind.sample(world, simTime).x * 0.35f * dt;
            }
        }
    }
    for (int z = 0; z < depth_; ++z) {
        for (int j = 0; j <= height_; ++j) {
            for (int x = 0; x < width_; ++x) {
                if (solid_.size() && ((j > 0 && solid_[cellIndex(x, j - 1, z)]) ||
                                      (j < height_ && solid_[cellIndex(x, j, z)])))
                    continue;
                const glm::vec3 world =
                    bounds_.minimum + glm::vec3((x + 0.5f) * cellSize_.x,
                                                static_cast<float>(j) * cellSize_.y,
                                                (z + 0.5f) * cellSize_.z);
                v_[vIndex(x, j, z)] += wind.sample(world, simTime).y * 0.35f * dt;
            }
        }
    }
    for (int k = 0; k <= depth_; ++k) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (solid_.size() && ((k > 0 && solid_[cellIndex(x, y, k - 1)]) ||
                                      (k < depth_ && solid_[cellIndex(x, y, k)])))
                    continue;
                const glm::vec3 world =
                    bounds_.minimum + glm::vec3((x + 0.5f) * cellSize_.x, (y + 0.5f) * cellSize_.y,
                                                static_cast<float>(k) * cellSize_.z);
                w_[wIndex(x, y, k)] += wind.sample(world, simTime).z * 0.35f * dt;
            }
        }
    }
}

void MacFluidGrid::advectVelocity(float dt) {
    tmpU_ = u_;
    tmpV_ = v_;
    tmpW_ = w_;
    const glm::vec3 invCell(1.f / cellSize_.x, 1.f / cellSize_.y, 1.f / cellSize_.z);

    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int i = 1; i < width_; ++i) {
                const glm::vec3 pos(static_cast<float>(i) - 0.5f, static_cast<float>(y),
                                    static_cast<float>(z));
                const glm::vec3 vel = sampleVelocity(pos);
                const glm::vec3 back =
                    pos - glm::vec3(vel.x * invCell.x, vel.y * invCell.y, vel.z * invCell.z) * dt;
                const glm::vec3 clamped(std::clamp(back.x, 0.f, static_cast<float>(width_ - 1)),
                                        std::clamp(back.y, 0.f, static_cast<float>(height_ - 1)),
                                        std::clamp(back.z, 0.f, static_cast<float>(depth_ - 1)));
                tmpU_[uIndex(i, y, z)] = sampleVelocity(clamped).x;
            }
        }
    }
    for (int z = 0; z < depth_; ++z) {
        for (int j = 1; j < height_; ++j) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 pos(static_cast<float>(x), static_cast<float>(j) - 0.5f,
                                    static_cast<float>(z));
                const glm::vec3 vel = sampleVelocity(pos);
                const glm::vec3 back =
                    pos - glm::vec3(vel.x * invCell.x, vel.y * invCell.y, vel.z * invCell.z) * dt;
                const glm::vec3 clamped(std::clamp(back.x, 0.f, static_cast<float>(width_ - 1)),
                                        std::clamp(back.y, 0.f, static_cast<float>(height_ - 1)),
                                        std::clamp(back.z, 0.f, static_cast<float>(depth_ - 1)));
                tmpV_[vIndex(x, j, z)] = sampleVelocity(clamped).y;
            }
        }
    }
    for (int k = 1; k < depth_; ++k) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 pos(static_cast<float>(x), static_cast<float>(y),
                                    static_cast<float>(k) - 0.5f);
                const glm::vec3 vel = sampleVelocity(pos);
                const glm::vec3 back =
                    pos - glm::vec3(vel.x * invCell.x, vel.y * invCell.y, vel.z * invCell.z) * dt;
                const glm::vec3 clamped(std::clamp(back.x, 0.f, static_cast<float>(width_ - 1)),
                                        std::clamp(back.y, 0.f, static_cast<float>(height_ - 1)),
                                        std::clamp(back.z, 0.f, static_cast<float>(depth_ - 1)));
                tmpW_[wIndex(x, y, k)] = sampleVelocity(clamped).z;
            }
        }
    }
    u_.swap(tmpU_);
    v_.swap(tmpV_);
    w_.swap(tmpW_);
}

void MacFluidGrid::projectPressure() {
    // Gauss–Seidel projection toward near-incompressible flow.
    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (solid_[cellIndex(x, y, z)]) {
                    divergence_[cellIndex(x, y, z)] = 0.f;
                    continue;
                }
                const float du = (uAt(x + 1, y, z) - uAt(x, y, z)) / cellSize_.x;
                const float dv = (vAt(x, y + 1, z) - vAt(x, y, z)) / cellSize_.y;
                const float dw = (wAt(x, y, z + 1) - wAt(x, y, z)) / cellSize_.z;
                divergence_[cellIndex(x, y, z)] = du + dv + dw;
                pressure_[cellIndex(x, y, z)] = 0.f;
            }
        }
    }

    constexpr int kIters = 40;
    for (int iter = 0; iter < kIters; ++iter) {
        for (int z = 0; z < depth_; ++z) {
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    if (solid_[cellIndex(x, y, z)]) continue;
                    const float pL = x > 0 ? pressure_[cellIndex(x - 1, y, z)] : pressure_[cellIndex(x, y, z)];
                    const float pR =
                        x + 1 < width_ ? pressure_[cellIndex(x + 1, y, z)] : pressure_[cellIndex(x, y, z)];
                    const float pD = y > 0 ? pressure_[cellIndex(x, y - 1, z)] : pressure_[cellIndex(x, y, z)];
                    const float pU =
                        y + 1 < height_ ? pressure_[cellIndex(x, y + 1, z)] : pressure_[cellIndex(x, y, z)];
                    const float pB = z > 0 ? pressure_[cellIndex(x, y, z - 1)] : pressure_[cellIndex(x, y, z)];
                    const float pF =
                        z + 1 < depth_ ? pressure_[cellIndex(x, y, z + 1)] : pressure_[cellIndex(x, y, z)];
                    pressure_[cellIndex(x, y, z)] =
                        (pL + pR + pD + pU + pB + pF - divergence_[cellIndex(x, y, z)] * cellSize_.x *
                                                            cellSize_.x) /
                        6.f;
                }
            }
        }
    }

    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int i = 1; i < width_; ++i) {
                const float grad =
                    (pressure_[cellIndex(i, y, z)] - pressure_[cellIndex(i - 1, y, z)]) / cellSize_.x;
                u_[uIndex(i, y, z)] -= grad;
            }
        }
    }
    for (int z = 0; z < depth_; ++z) {
        for (int j = 1; j < height_; ++j) {
            for (int x = 0; x < width_; ++x) {
                const float grad =
                    (pressure_[cellIndex(x, j, z)] - pressure_[cellIndex(x, j - 1, z)]) / cellSize_.y;
                v_[vIndex(x, j, z)] -= grad;
            }
        }
    }
    for (int k = 1; k < depth_; ++k) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const float grad =
                    (pressure_[cellIndex(x, y, k)] - pressure_[cellIndex(x, y, k - 1)]) / cellSize_.z;
                w_[wIndex(x, y, k)] -= grad;
            }
        }
    }
}

void MacFluidGrid::advectDensity(float dt, const FogInteractor* interactor) {
    const glm::vec3 invCell(1.f / cellSize_.x, 1.f / cellSize_.y, 1.f / cellSize_.z);
    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (solid_[cellIndex(x, y, z)]) {
                    tmpDensity_[cellIndex(x, y, z)] = 0.f;
                    continue;
                }
                const glm::vec3 pos(static_cast<float>(x), static_cast<float>(y),
                                    static_cast<float>(z));
                const glm::vec3 vel = velocityAtCell(x, y, z);
                const glm::vec3 delta =
                    -glm::vec3(vel.x * invCell.x, vel.y * invCell.y, vel.z * invCell.z) * dt;
                const glm::vec3 back = traceBounded(pos, delta, interactor);
                const glm::vec3 clamped(std::clamp(back.x, 0.f, static_cast<float>(width_ - 1)),
                                        std::clamp(back.y, 0.f, static_cast<float>(height_ - 1)),
                                        std::clamp(back.z, 0.f, static_cast<float>(depth_ - 1)));
                tmpDensity_[cellIndex(x, y, z)] = sampleDensityLocal(clamped);
            }
        }
    }
    density_.swap(tmpDensity_);
}

Result<FogCflReport> MacFluidGrid::step(float dt, SceneWind& wind, FogInteractor* interactor,
                                        float simTime) {
    if (width_ <= 0) {
        return Result<FogCflReport>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "MAC grid is not configured", "step", {}, "graphics.fog"));
    }
    if (!std::isfinite(dt) || dt < 0.f) {
        return Result<FogCflReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dt must be finite and >= 0", "dt", {}, "graphics.fog"));
    }

    auto windTick = wind.tick(dt);
    if (!windTick.ok()) return Result<FogCflReport>::failure(windTick.status());

    accumulator_ = std::min(accumulator_ + dt, kMaxAccumulated);
    float time = simTime;
    while (accumulator_ + 1e-8f >= fixedDt_) {
        if (interactor) {
            auto applied = interactor->applyToGrid(*this, fixedDt_);
            if (!applied.ok()) return Result<FogCflReport>::failure(applied.status());
        }
        applyForcesAndWind(wind, fixedDt_, time);
        advectVelocity(fixedDt_);
        projectPressure();
        advectDensity(fixedDt_, interactor);
        accumulator_ -= fixedDt_;
        time += fixedDt_;
        ++stepCount_;
    }
    return Result<FogCflReport>::success(diagnoseCfl());
}

}  // namespace eve::graphics::fog
