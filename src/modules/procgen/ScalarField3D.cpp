#include "procgen/ScalarField3D.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {
Result<void> invalidField() {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   "scalar field requires finite samples and axis sizes in [2,256]", {},
                                                   {}, "procgen.field"));
}
bool validSize(int x, int y, int z) { return x >= 2 && y >= 2 && z >= 2 && x <= 256 && y <= 256 && z <= 256; }
}  // namespace

Result<void> ScalarField3D::resize(int x, int y, int z, float value) {
    if (!validSize(x, y, z) || !std::isfinite(value)) return invalidField();
    std::vector<float> candidate(size_t(x) * size_t(y) * size_t(z), value);
    samples_ = std::move(candidate);
    nx_      = x;
    ny_      = y;
    nz_      = z;
    return Result<void>::success();
}

Result<void> ScalarField3D::assign(int x, int y, int z, std::span<const float> values) {
    if (!validSize(x, y, z) || values.size() != size_t(x) * size_t(y) * size_t(z) ||
        !std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); }))
        return invalidField();
    std::vector<float> candidate(values.begin(), values.end());
    samples_ = std::move(candidate);
    nx_      = x;
    ny_      = y;
    nz_      = z;
    return Result<void>::success();
}

bool ScalarField3D::contains(int x, int y, int z) const noexcept {
    return x >= 0 && y >= 0 && z >= 0 && x < nx_ && y < ny_ && z < nz_;
}

Result<float> ScalarField3D::sample(int x, int y, int z) const {
    if (!contains(x, y, z))
        return Result<float>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "scalar sample is outside the volume", {}, {}, "procgen.field"));
    return Result<float>::success(samples_[size_t(x) + size_t(nx_) * (size_t(y) + size_t(ny_) * size_t(z))]);
}

Result<void> ScalarField3D::setSample(int x, int y, int z, float value) {
    if (!contains(x, y, z) || !std::isfinite(value)) return invalidField();
    samples_[size_t(x) + size_t(nx_) * (size_t(y) + size_t(ny_) * size_t(z))] = value;
    return Result<void>::success();
}

}  // namespace eve::procgen
