// eve-hot-path
#include "spatial/multiscale/CellPosition.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace eve::spatial {
namespace {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

Result<CellProjection> failure(DiagnosticCode code, const char* message) {
    return Result<CellProjection>::failure(Diagnostic::error(code, message, "spatial.cellProjection"));
}
}  // namespace

Result<CellPosition> normalizeCellPosition(CellPosition position) {
    constexpr uint64_t sign = uint64_t{1} << 63;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double fraction = position.fraction[axis];
        if (!std::isfinite(fraction))
            return Result<CellPosition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Local offset must be finite", "spatial.cellNormalization"));
        double integral  = 0.0;
        double remainder = std::modf(fraction, &integral);
        // Avoid floor(fraction + 0.5): that sum rounds the value immediately
        // below 0.5 to 1.0. Here carries are exact whenever a fraction exists.
        if (remainder >= 0.5) {
            integral += 1.0;
            remainder -= 1.0;
        } else if (remainder < -0.5) {
            integral -= 1.0;
            remainder += 1.0;
        }
        const double magnitude = std::abs(integral);
        if (magnitude >= 0x1p64)
            return Result<CellPosition>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Cell carry exceeds address range", "spatial.cellNormalization"));
        const auto carry   = static_cast<uint64_t>(magnitude);
        auto       ordered = static_cast<uint64_t>(position.cell[axis]) ^ sign;
        if ((integral < 0.0 && carry > ordered) ||
            (integral >= 0.0 && carry > std::numeric_limits<uint64_t>::max() - ordered))
            return Result<CellPosition>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Normalized address overflows int64", "spatial.cellNormalization"));
        ordered = integral < 0.0 ? ordered - carry : ordered + carry;
        // Both signed conversions have an in-range operand; no implementation-defined wrap.
        position.cell[axis] =
            ordered >= sign ? static_cast<int64_t>(ordered - sign) : INT64_MIN + static_cast<int64_t>(ordered);
        position.fraction[axis] = remainder == 0.0 ? 0.0 : remainder;
    }
    return Result<CellPosition>::success(position);
}

Result<CellProjection> projectCellRelative(CellPosition position, CellPosition origin, CellProjectionPolicy policy) {
    if (policy.cellExponent < -900 || policy.cellExponent > 900 || policy.outputExponent < -900 ||
        policy.outputExponent > 900 || !std::isfinite(policy.maxAbsoluteError) || policy.maxAbsoluteError <= 0.0) {
        return failure(DiagnosticCode::InvalidArgument, "Invalid projection scale or absolute error budget");
    }
    CellProjection   result;
    const int        shift   = policy.cellExponent - policy.outputExponent;
    constexpr double epsilon = std::numeric_limits<double>::epsilon();
    constexpr double tiny    = std::numeric_limits<double>::denorm_min();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double target = position.fraction[axis];
        const double base   = origin.fraction[axis];
        if (!std::isfinite(target) || !std::isfinite(base) || target < -0.5 || target >= 0.5 || base < -0.5 ||
            base >= 0.5) {
            return failure(DiagnosticCode::InvalidArgument, "Fractions must be finite and in [-0.5, 0.5)");
        }
        const int64_t a = position.cell[axis];
        const int64_t b = origin.cell[axis];
        // Ordered unsigned subtraction yields the full magnitude even across INT64_MIN/MAX.
        // Subtracting after converting the absolute addresses to double would lose local offsets.
        const uint64_t magnitude = a >= b ? static_cast<uint64_t>(a) - static_cast<uint64_t>(b)
                                          : static_cast<uint64_t>(b) - static_cast<uint64_t>(a);
        if (magnitude == 0 && target == base) continue;
        const double coarse = (a >= b ? 1.0 : -1.0) * static_cast<double>(magnitude);
        const double fine   = target - base;
        result.offset[axis] = shift == 0 ? coarse + fine : std::ldexp(coarse + fine, shift);
        // Covers integer conversion, subtraction, addition and final scaling, including cancellation.
        // The factor also leaves headroom for rounding while computing the bound itself.
        const double unscaledBound      = 8.0 * epsilon * (std::abs(coarse) + std::abs(target) + std::abs(base));
        const double scaledBound        = shift == 0 ? unscaledBound : std::ldexp(unscaledBound, shift);
        result.absoluteErrorBound[axis] = std::nextafter(scaledBound + tiny, std::numeric_limits<double>::infinity());
        if (!std::isfinite(result.offset[axis]) || !std::isfinite(result.absoluteErrorBound[axis]) ||
            result.absoluteErrorBound[axis] > policy.maxAbsoluteError) {
            return failure(DiagnosticCode::Unsupported, "Projection cannot satisfy the requested range or precision");
        }
    }
    return Result<CellProjection>::success(result);
}

}  // namespace eve::spatial
