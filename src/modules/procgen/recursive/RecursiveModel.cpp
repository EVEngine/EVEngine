// eve-hot-path
#include "procgen/recursive/RecursiveModel.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <new>
#include <utility>

namespace eve::procgen::recursive {
namespace {
template <class T>
Result<T> failed(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "procgen.recursive"));
}
std::uint64_t mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
ScaledMass scaled(double value, int exponent) {
    int        carry    = 0;
    const auto mantissa = std::frexp(value, &carry);
    return {mantissa, exponent + carry};
}
NodeSummary child(NodeSummary parent, unsigned side, unsigned axis, ScaledMass rootMass, std::uint64_t capacity) {
    const auto   lower = parent.leafCount / 2;
    const double ratio = static_cast<double>(lower) / static_cast<double>(parent.leafCount);
    const double half  = parent.halfExtent[axis];
    parent.key.path    = parent.key.path * 2 + side;
    if (side == 0) {
        parent.leafCount        = lower;
        parent.halfExtent[axis] = half * ratio;
        parent.center[axis] -= half * (1.0 - ratio);
    } else {
        parent.firstLeaf += lower;
        parent.leafCount -= lower;
        parent.halfExtent[axis] = half * (1.0 - ratio);
        parent.center[axis] += half * ratio;
    }
    parent.rootMassFraction = static_cast<double>(parent.leafCount) / static_cast<double>(capacity);
    parent.mass             = scaled(rootMass.mantissa * parent.rootMassFraction, rootMass.exponent);
    return parent;
}
unsigned splitAxis(const NodeSummary& summary) {
    unsigned axis = 0;
    for (unsigned i = 1; i < 3; ++i)
        if (summary.halfExtent[i] > summary.halfExtent[axis]) axis = i;
    return axis;
}
}  // namespace
struct RecursiveModel::Impl {
    ModelDefinition definition;
    NodeSummary     root;
};
RecursiveModel::RecursiveModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RecursiveModel::~RecursiveModel()                                    = default;
RecursiveModel::RecursiveModel(RecursiveModel&&) noexcept            = default;
RecursiveModel& RecursiveModel::operator=(RecursiveModel&&) noexcept = default;

Result<RecursiveModel> RecursiveModel::build(ModelDefinition definition) {
    if (definition.ruleVersion != 1)
        return failed<RecursiveModel>(DiagnosticCode::UnknownVersion, "Unsupported recursive rule version");
    if (definition.capacity == 0 || definition.capacity > (uint64_t{1} << 52) || definition.lengthExponent < -900 ||
        definition.lengthExponent > 900 || !std::isfinite(definition.density) || definition.density <= 0 ||
        (definition.capacityMode != CapacityMode::FixedVolume &&
         definition.capacityMode != CapacityMode::FixedMeanLeafVolume))
        return failed<RecursiveModel>(DiagnosticCode::InvalidArgument,
                                      "Invalid capacity, scale, density or growth mode");
    for (double half : definition.baseHalfExtent)
        if (!std::isfinite(half) || half < 0x1p-100 || half > 0x1p100)
            return failed<RecursiveModel>(DiagnosticCode::Unsupported,
                                          "Local half extents require normalized root units");
    try {
        auto impl             = std::make_unique<Impl>();
        impl->definition      = definition;
        auto& root            = impl->root;
        root.leafCount        = definition.capacity;
        root.lengthExponent   = definition.lengthExponent;
        root.revision         = definition.revision;
        root.rootMassFraction = 1;
        const double growth   = definition.capacityMode == CapacityMode::FixedMeanLeafVolume
                                    ? std::cbrt(static_cast<double>(definition.capacity))
                                    : 1.0;
        ScaledMass   mass     = scaled(definition.density, 3 * definition.lengthExponent + 3);
        for (unsigned axis = 0; axis < 3; ++axis) {
            root.halfExtent[axis] = definition.baseHalfExtent[axis] * growth;
            mass                  = scaled(mass.mantissa * definition.baseHalfExtent[axis], mass.exponent);
        }
        if (definition.capacityMode == CapacityMode::FixedMeanLeafVolume)
            mass = scaled(mass.mantissa * static_cast<double>(definition.capacity), mass.exponent);
        root.mass = mass;
        return Result<RecursiveModel>::success(RecursiveModel(std::move(impl)));
    } catch (const std::bad_alloc&) {
        return failed<RecursiveModel>(DiagnosticCode::Failed, "Recursive definition allocation failed");
    }
}
Result<NodeSummary> RecursiveModel::describe(NodeKey key) const {
    if (!impl_) return failed<NodeSummary>(DiagnosticCode::StaleHandle, "Model has been moved from");
    if (key.path == 0 || std::bit_width(key.path) > 53)
        return failed<NodeSummary>(DiagnosticCode::InvalidArgument, "Invalid or over-depth node path");
    auto summary = impl_->root;
    for (int bit = static_cast<int>(std::bit_width(key.path)) - 2; bit >= 0; --bit) {
        if (summary.leafCount == 1)
            return failed<NodeSummary>(DiagnosticCode::NotFound, "Path descends below a terminal leaf");
        summary = child(summary, static_cast<unsigned>((key.path >> bit) & 1), splitAxis(summary), impl_->root.mass,
                        impl_->definition.capacity);
    }
    return Result<NodeSummary>::success(summary);
}
Result<NodeSummary> RecursiveModel::leaf(std::uint64_t ordinal) const {
    if (!impl_) return failed<NodeSummary>(DiagnosticCode::StaleHandle, "Model has been moved from");
    if (ordinal >= impl_->definition.capacity)
        return failed<NodeSummary>(DiagnosticCode::InvalidArgument, "Leaf ordinal outside model capacity");
    auto summary = impl_->root;
    while (summary.leafCount > 1) {
        const unsigned side = ordinal < summary.firstLeaf + summary.leafCount / 2 ? 0u : 1u;
        summary             = child(summary, side, splitAxis(summary), impl_->root.mass, impl_->definition.capacity);
    }
    return Result<NodeSummary>::success(summary);
}

Result<std::array<NodeSummary, 2>> RecursiveModel::split(NodeKey key) const {
    auto parent = describe(key);
    if (!parent.ok()) return Result<std::array<NodeSummary, 2>>::failure(parent.status());
    if (parent.value().leafCount == 1)
        return failed<std::array<NodeSummary, 2>>(DiagnosticCode::PreconditionViolation, "Terminal node cannot split");
    const auto axis = splitAxis(parent.value());
    return Result<std::array<NodeSummary, 2>>::success(
        {child(parent.value(), 0, axis, impl_->root.mass, impl_->definition.capacity),
         child(parent.value(), 1, axis, impl_->root.mass, impl_->definition.capacity)});
}
Result<std::vector<NodeSummary>> RecursiveModel::prepareFrontier(std::span<const NodeKey> keys,
                                                                 std::size_t              maxNodes) const {
    if (!impl_) return failed<std::vector<NodeSummary>>(DiagnosticCode::StaleHandle, "Model has been moved from");
    if (keys.empty() || keys.size() > maxNodes || maxNodes > 1000000)
        return failed<std::vector<NodeSummary>>(DiagnosticCode::InvalidArgument, "Invalid active frontier budget");
    try {
        std::vector<NodeSummary> result;
        result.reserve(keys.size());
        for (auto key : keys) {
            auto summary = describe(key);
            if (!summary.ok()) return Result<std::vector<NodeSummary>>::failure(summary.status());
            result.push_back(summary.value());
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.firstLeaf < b.firstLeaf; });
        std::uint64_t cursor = 0;
        for (const auto& summary : result) {
            if (summary.firstLeaf != cursor)
                return failed<std::vector<NodeSummary>>(DiagnosticCode::Conflict,
                                                        "Frontier duplicates or omits source coverage");
            cursor += summary.leafCount;
        }
        if (cursor != impl_->definition.capacity)
            return failed<std::vector<NodeSummary>>(DiagnosticCode::Conflict, "Frontier omits source coverage");
        return Result<std::vector<NodeSummary>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return failed<std::vector<NodeSummary>>(DiagnosticCode::Failed, "Frontier allocation failed");
    }
}
Result<std::uint64_t> RecursiveModel::randomWord(NodeKey key, RandomStream stream, std::uint64_t counter) const {
    auto summary = describe(key);
    if (!summary.ok()) return Result<std::uint64_t>::failure(summary.status());
    if (stream != RandomStream::Geometry && stream != RandomStream::Material && stream != RandomStream::Decoration)
        return failed<std::uint64_t>(DiagnosticCode::InvalidArgument, "Unknown random stream");
    auto state = mix(impl_->definition.seed ^ 0x6576657265637631ULL);
    state      = mix(state ^ impl_->definition.ruleVersion);
    state      = mix(state ^ key.path);
    state      = mix(state ^ static_cast<std::uint64_t>(stream));
    return Result<std::uint64_t>::success(mix(state ^ counter));
}
}  // namespace eve::procgen::recursive
