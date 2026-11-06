// eve-hot-path
#include "spatial/multiscale/FrameGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>
#include <vector>

namespace eve::spatial {
namespace {
constexpr std::array<double, 9> identityRotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
constexpr double                infinity = std::numeric_limits<double>::infinity();
struct Interval {
    double lo = 0, hi = 0;
};
using RotationBounds = std::array<Interval, 9>;
Interval add(Interval a, Interval b) {
    if (a.lo == 0 && a.hi == 0) return b;
    if (b.lo == 0 && b.hi == 0) return a;
    return {std::nextafter(a.lo + b.lo, -infinity), std::nextafter(a.hi + b.hi, infinity)};
}
Interval multiply(Interval a, Interval b) {
    if ((a.lo == 0 && a.hi == 0) || (b.lo == 0 && b.hi == 0)) return {};
    if (a.lo == 1 && a.hi == 1) return b;
    if (b.lo == 1 && b.hi == 1) return a;
    if (a.lo == -1 && a.hi == -1) return {-b.hi, -b.lo};
    if (b.lo == -1 && b.hi == -1) return {-a.hi, -a.lo};
    const std::array<double, 4> products{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    return {std::nextafter(*std::min_element(products.begin(), products.end()), -infinity),
            std::nextafter(*std::max_element(products.begin(), products.end()), infinity)};
}
RotationBounds exactRotation(const std::array<double, 9>& matrix) {
    RotationBounds result;
    for (unsigned i = 0; i < 9; ++i) result[i] = {matrix[i], matrix[i]};
    return result;
}
RotationBounds compose(const RotationBounds& parent, const RotationBounds& local) {
    RotationBounds result;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            for (unsigned k = 0; k < 3; ++k)
                result[row * 3 + col] = add(result[row * 3 + col], multiply(parent[row * 3 + k], local[k * 3 + col]));
    return result;
}
bool validRotation(const std::array<double, 9>& matrix) {
    for (double value : matrix)
        if (!std::isfinite(value) || std::abs(value) > 1.000000000001) return false;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) dot += matrix[i * 3 + k] * matrix[j * 3 + k];
            if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 1e-12) return false;
        }
    const double det = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
                       matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
                       matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    return std::abs(det - 1.0) <= 1e-12;
}
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "spatial.frameGraph"));
}
struct FrameNode {
    FrameDefinition definition;
    std::size_t     inputIndex          = 0;
    unsigned        depth               = 0;
    int             absoluteExponent    = 0;
    bool            identityOrientation = true;
    RotationBounds  rootOrientation     = exactRotation(identityRotation);
};
Result<CellProjection> orient(CellProjection input, const FrameNode& node, double budget) {
    if (node.identityOrientation) return Result<CellProjection>::success(input);
    CellProjection result;
    for (unsigned row = 0; row < 3; ++row) {
        Interval sum;
        for (unsigned k = 0; k < 3; ++k) {
            const Interval value =
                input.absoluteErrorBound[k] == 0
                    ? Interval{input.offset[k], input.offset[k]}
                    : Interval{std::nextafter(input.offset[k] - input.absoluteErrorBound[k], -infinity),
                               std::nextafter(input.offset[k] + input.absoluteErrorBound[k], infinity)};
            if (!std::isfinite(value.lo) || !std::isfinite(value.hi))
                return failure<CellProjection>(DiagnosticCode::Unsupported, "Oriented input interval range exceeded");
            sum = add(sum, multiply(node.rootOrientation[row * 3 + k], value));
        }
        if (!std::isfinite(sum.lo) || !std::isfinite(sum.hi))
            return failure<CellProjection>(DiagnosticCode::Unsupported, "Oriented projection range exceeded");
        const double midpoint = std::midpoint(sum.lo, sum.hi);
        const double error =
            sum.lo == sum.hi ? 0.0 : std::nextafter(std::max(midpoint - sum.lo, sum.hi - midpoint), infinity);
        if (!std::isfinite(error) || error > budget)
            return failure<CellProjection>(DiagnosticCode::Unsupported,
                                           "Oriented projection cannot meet precision budget");
        result.offset[row]             = midpoint;
        result.absoluteErrorBound[row] = error;
    }
    return Result<CellProjection>::success(result);
}
}  // namespace
struct FrameGraph::Impl {
    script::RuntimeObjectRegistry<FrameNode, MultiscaleFrameTag> registry;
    std::vector<FrameHandle>                                     handles;
};
FrameGraph::FrameGraph(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FrameGraph::~FrameGraph()                                = default;
FrameGraph::FrameGraph(FrameGraph&&) noexcept            = default;
FrameGraph& FrameGraph::operator=(FrameGraph&&) noexcept = default;

Result<FrameGraph> FrameGraph::build(std::span<const FrameDefinition> definitions, std::size_t maxFrames) {
    if (maxFrames == 0 || maxFrames > 1000000 || definitions.empty() || definitions.size() > maxFrames)
        return failure<FrameGraph>(DiagnosticCode::InvalidArgument, "Invalid frame count or budget");
    try {
        auto impl = std::make_unique<Impl>();
        impl->handles.reserve(definitions.size());
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            if (!validRotation(definitions[i].rotation))
                return failure<FrameGraph>(DiagnosticCode::InvalidArgument,
                                           "Frame orientation must be proper orthonormal");
            auto normalized = normalizeCellPosition(definitions[i].translation);
            if (!normalized.ok()) return Result<FrameGraph>::failure(normalized.status());
            auto node                    = std::make_unique<FrameNode>();
            node->inputIndex             = i;
            node->definition             = definitions[i];
            node->definition.translation = normalized.value();
            if (i == 0) {
                if (node->definition.parentIndex != 0 || node->definition.exponent != 0 ||
                    node->definition.translation.cell != std::array<int64_t, 3>{} ||
                    node->definition.translation.fraction != std::array<double, 3>{} ||
                    node->definition.rotation != identityRotation)
                    return failure<FrameGraph>(DiagnosticCode::InvalidArgument, "Root frame must be identity");
            } else {
                if (node->definition.parentIndex >= i || node->definition.exponent < -900 ||
                    node->definition.exponent > 900)
                    return failure<FrameGraph>(DiagnosticCode::InvalidArgument,
                                               "Parents must precede children with bounded scale");
                const auto parent      = impl->registry.resolve(impl->handles[node->definition.parentIndex]);
                node->depth            = parent->depth + 1;
                node->absoluteExponent = parent->absoluteExponent + node->definition.exponent;
                node->identityOrientation =
                    parent->identityOrientation && node->definition.rotation == identityRotation;
                if (node->definition.rotation == identityRotation)
                    node->rootOrientation = parent->rootOrientation;
                else if (parent->identityOrientation)
                    node->rootOrientation = exactRotation(node->definition.rotation);
                else
                    node->rootOrientation = compose(parent->rootOrientation, exactRotation(node->definition.rotation));
                if (node->depth > 64 || node->absoluteExponent < -900 || node->absoluteExponent > 900)
                    return failure<FrameGraph>(DiagnosticCode::Unsupported,
                                               "Hierarchy depth or cumulative scale exceeded");
            }
            auto inserted = impl->registry.emplace(std::move(node));
            if (!inserted.ok()) return Result<FrameGraph>::failure(inserted.status());
            impl->handles.push_back(inserted.value());
        }
        return Result<FrameGraph>::success(FrameGraph(std::move(impl)));
    } catch (const std::bad_alloc&) {
        return failure<FrameGraph>(DiagnosticCode::Failed, "Frame graph allocation failed");
    }
}

Result<FrameHandle> FrameGraph::frame(std::size_t inputIndex) const {
    if (!impl_ || inputIndex >= impl_->handles.size())
        return failure<FrameHandle>(DiagnosticCode::InvalidArgument, "Frame input index unavailable");
    return Result<FrameHandle>::success(impl_->handles[inputIndex]);
}

Result<CellProjection> FrameGraph::project(FramePosition target, FramePosition origin, int outputExponent,
                                           double maxAbsoluteError) const {
    if (!impl_) return failure<CellProjection>(DiagnosticCode::StaleHandle, "Graph has been moved from");
    const auto targetNode = impl_->registry.resolve(target.frame);
    const auto originNode = impl_->registry.resolve(origin.frame);
    if (!targetNode.isBound() || !originNode.isBound())
        return failure<CellProjection>(DiagnosticCode::StaleHandle, "Frame belongs to an old or foreign graph");
    if (outputExponent < -900 || outputExponent > 900 || !std::isfinite(maxAbsoluteError) || maxAbsoluteError <= 0)
        return failure<CellProjection>(DiagnosticCode::InvalidArgument, "Invalid output unit or precision budget");
    if (target.frame == origin.frame) {
        auto local = projectCellRelative(target.position, origin.position,
                                         {targetNode->absoluteExponent, outputExponent, maxAbsoluteError});
        if (!local.ok()) return local;
        return orient(local.value(), *targetNode, maxAbsoluteError);
    }
    auto a     = targetNode->inputIndex;
    auto b     = originNode->inputIndex;
    auto nodeA = targetNode;
    auto nodeB = originNode;
    while (a != b) {
        if (nodeA->depth >= nodeB->depth) {
            a     = nodeA->definition.parentIndex;
            nodeA = impl_->registry.resolve(impl_->handles[a]);
        } else {
            b     = nodeB->definition.parentIndex;
            nodeB = impl_->registry.resolve(impl_->handles[b]);
        }
    }
    const auto     ancestor = a;
    CellProjection result;
    const auto     append = [&](CellPosition position, const FrameNode& frameNode, double sign) -> Result<void> {
        auto local = projectCellRelative(position, {}, {frameNode.absoluteExponent, outputExponent, maxAbsoluteError});
        if (!local.ok()) return Result<void>::failure(local.status());
        auto term = orient(local.value(), frameNode, maxAbsoluteError);
        if (!term.ok()) return Result<void>::failure(term.status());
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto   value = term.value().offset[axis];
            const double error =
                result.absoluteErrorBound[axis] + term.value().absoluteErrorBound[axis] +
                8 * std::numeric_limits<double>::epsilon() * (std::abs(result.offset[axis]) + std::abs(value));
            result.offset[axis] += sign * value;
            result.absoluteErrorBound[axis] = std::nextafter(error, std::numeric_limits<double>::infinity());
            if (!std::isfinite(result.offset[axis]) || !std::isfinite(result.absoluteErrorBound[axis]) ||
                result.absoluteErrorBound[axis] > maxAbsoluteError)
                return failure<void>(DiagnosticCode::Unsupported, "Branch projection cannot meet precision budget");
        }
        return Result<void>::success();
    };
    for (unsigned side = 0; side < 2; ++side) {
        const auto   point = side == 0 ? target : origin;
        const double sign  = side == 0 ? 1.0 : -1.0;
        auto         node  = impl_->registry.resolve(point.frame);
        auto         added = append(point.position, *node, sign);
        if (!added.ok()) return Result<CellProjection>::failure(added.status());
        auto index = node->inputIndex;
        while (index != ancestor) {
            const auto parentIndex = node->definition.parentIndex;
            const auto parent      = impl_->registry.resolve(impl_->handles[parentIndex]);
            auto       translated  = append(node->definition.translation, *parent, sign);
            if (!translated.ok()) return Result<CellProjection>::failure(translated.status());
            index = parentIndex;
            node  = parent;
        }
    }
    return Result<CellProjection>::success(result);
}
Result<FrameRelocation> FrameGraph::reexpress(FramePosition source, FrameHandle destination,
                                              double maxAbsoluteError) const {
    if (!impl_) return failure<FrameRelocation>(DiagnosticCode::StaleHandle, "Graph has been moved from");
    const auto sourceNode      = impl_->registry.resolve(source.frame);
    const auto destinationNode = impl_->registry.resolve(destination);
    if (!sourceNode.isBound() || !destinationNode.isBound())
        return failure<FrameRelocation>(DiagnosticCode::StaleHandle, "Frame belongs to an old or foreign graph");
    if (!std::isfinite(maxAbsoluteError) || maxAbsoluteError <= 0)
        return failure<FrameRelocation>(DiagnosticCode::InvalidArgument, "Invalid relocation precision budget");
    if (source.frame == destination) {
        auto normalized = normalizeCellPosition(source.position);
        if (!normalized.ok()) return Result<FrameRelocation>::failure(normalized.status());
        return Result<FrameRelocation>::success({{destination, normalized.value()}, {}});
    }
    auto relative = project(source, {destination, {}}, destinationNode->absoluteExponent, maxAbsoluteError);
    if (!relative.ok()) return Result<FrameRelocation>::failure(relative.status());
    auto local = relative.value();
    if (!destinationNode->identityOrientation) {
        const auto&    matrix = destinationNode->rootOrientation;
        RotationBounds adjugate;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col) {
                const auto r1 = (col + 1) % 3, r2 = (col + 2) % 3;
                const auto c1 = (row + 1) % 3, c2 = (row + 2) % 3;
                const auto positive     = multiply(matrix[r1 * 3 + c1], matrix[r2 * 3 + c2]);
                const auto negative     = multiply(matrix[r1 * 3 + c2], matrix[r2 * 3 + c1]);
                adjugate[row * 3 + col] = add(positive, {-negative.hi, -negative.lo});
            }
        Interval determinant;
        for (unsigned k = 0; k < 3; ++k) determinant = add(determinant, multiply(matrix[k], adjugate[k * 3]));
        if (!std::isfinite(determinant.lo) || !std::isfinite(determinant.hi) ||
            (determinant.lo <= 0 && determinant.hi >= 0))
            return failure<FrameRelocation>(DiagnosticCode::Unsupported,
                                            "Destination orientation inverse is uncertain");
        FrameNode inverse;
        inverse.identityOrientation = false;
        for (unsigned i = 0; i < 9; ++i) {
            const auto value = adjugate[i];
            if (determinant.lo == 1 && determinant.hi == 1) {
                inverse.rootOrientation[i] = value;
            } else if (value.lo == 0 && value.hi == 0) {
                inverse.rootOrientation[i] = {};
            } else {
                const std::array<double, 4> quotients{value.lo / determinant.lo, value.lo / determinant.hi,
                                                      value.hi / determinant.lo, value.hi / determinant.hi};
                inverse.rootOrientation[i] = {
                    std::nextafter(*std::min_element(quotients.begin(), quotients.end()), -infinity),
                    std::nextafter(*std::max_element(quotients.begin(), quotients.end()), infinity)};
            }
        }
        auto oriented = orient(local, inverse, maxAbsoluteError);
        if (!oriented.ok()) return Result<FrameRelocation>::failure(oriented.status());
        local = oriented.value();
    }
    CellPosition position;
    position.fraction = local.offset;
    auto normalized   = normalizeCellPosition(position);
    if (!normalized.ok()) return Result<FrameRelocation>::failure(normalized.status());
    return Result<FrameRelocation>::success({{destination, normalized.value()}, local.absoluteErrorBound});
}
}  // namespace eve::spatial
