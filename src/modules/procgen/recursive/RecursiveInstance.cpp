// eve-hot-path
#include "procgen/recursive/RecursiveInstance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <utility>

namespace eve::procgen::recursive {
namespace {
struct InstanceOwner {};
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "procgen.recursive.instance"));
}
struct State {
    std::shared_ptr<const RecursiveModel>      model;
    ModelDefinition                            definition;
    Revision                                   revision;
    std::vector<LeafEdit>                      edits;
    std::map<std::uint64_t, EditedNodeSummary> summaries;
};
EditedNodeSummary unedited(NodeSummary nominal, Revision revision) {
    EditedNodeSummary summary;
    summary.nominal      = nominal;
    summary.massFraction = nominal.rootMassFraction;
    summary.mass         = nominal.mass;
    summary.centerOfMass = nominal.center;
    summary.revision     = revision;
    for (unsigned axis = 0; axis < 3; ++axis) {
        summary.boundsMin[axis] = nominal.center[axis] - nominal.halfExtent[axis];
        summary.boundsMax[axis] = nominal.center[axis] + nominal.halfExtent[axis];
        summary.centralSecondMomentPerRootMass[axis] =
            nominal.rootMassFraction * nominal.halfExtent[axis] * nominal.halfExtent[axis] / 3;
    }
    return summary;
}
Result<void> compileEdits(State& state, EditBudget budget) {
    std::sort(state.edits.begin(), state.edits.end(),
              [](const auto& a, const auto& b) { return a.ordinal < b.ordinal; });
    const double capacity = static_cast<double>(state.definition.capacity);
    auto         root     = state.model->describe({1});
    if (!root.ok()) return Result<void>::failure(root.status());
    const auto assignMass = [&](EditedNodeSummary& summary) -> Result<void> {
        int          carry     = 0;
        const double localMass = root.value().mass.mantissa * summary.massFraction;
        if (!std::isfinite(localMass) || (summary.massFraction > 0 && localMass == 0))
            return fail<void>(DiagnosticCode::Unsupported, "Edited mass exceeds local representation");
        summary.mass.mantissa = std::frexp(localMass, &carry);
        summary.mass.exponent = summary.massFraction == 0 ? 0 : root.value().mass.exponent + carry;
        return Result<void>::success();
    };
    for (std::size_t i = 0; i < state.edits.size(); ++i) {
        const auto& edit = state.edits[i];
        if (edit.ordinal >= state.definition.capacity || (i != 0 && state.edits[i - 1].ordinal == edit.ordinal) ||
            !std::isfinite(edit.massScale) || edit.massScale < 0 || edit.massScale > 1e6)
            return fail<void>(DiagnosticCode::InvalidArgument, "Invalid or duplicate leaf override");
        auto leaf = state.model->leaf(edit.ordinal);
        if (!leaf.ok()) return Result<void>::failure(leaf.status());
        auto summary         = unedited(leaf.value(), state.revision);
        summary.massFraction = edit.massScale / capacity;
        for (auto& moment : summary.centralSecondMomentPerRootMass) moment *= edit.massScale;
        if (edit.massScale > 0 && summary.massFraction == 0)
            return fail<void>(DiagnosticCode::Unsupported, "Edited mass ratio underflows");
        auto mass = assignMass(summary);
        if (!mass.ok()) return mass;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(edit.displacement[axis]) || !std::isfinite(edit.velocity[axis]))
                return fail<void>(DiagnosticCode::InvalidArgument, "Override motion must be finite");
            summary.centerOfMass[axis]        = leaf.value().center[axis] + edit.displacement[axis];
            summary.momentumPerRootMass[axis] = summary.massFraction * edit.velocity[axis];
            const double lo                   = std::nextafter(summary.centerOfMass[axis], -INFINITY);
            const double hi                   = std::nextafter(summary.centerOfMass[axis], INFINITY);
            summary.boundsMin[axis]           = std::nextafter(lo - leaf.value().halfExtent[axis], -INFINITY);
            summary.boundsMax[axis]           = std::nextafter(hi + leaf.value().halfExtent[axis], INFINITY);
            if (!std::isfinite(summary.centerOfMass[axis]) || !std::isfinite(summary.momentumPerRootMass[axis]) ||
                !std::isfinite(summary.boundsMin[axis]) || !std::isfinite(summary.boundsMax[axis]))
                return fail<void>(DiagnosticCode::Unsupported, "Override position, momentum or bounds overflow");
        }
        for (auto path = leaf.value().key.path; path != 0; path >>= 1) {
            if (!state.summaries.contains(path)) {
                if (state.summaries.size() >= budget.maxSummaries)
                    return fail<void>(DiagnosticCode::Failed, "Ancestor summary budget exceeded");
                state.summaries.emplace(path, EditedNodeSummary{});
            }
        }
        state.summaries.at(leaf.value().key.path) = summary;
    }
    // Numeric path ordering puts descendants after ancestors. Reverse traversal
    // combines complete positive child summaries, never subtracting destroyed mass.
    for (auto it = state.summaries.rbegin(); it != state.summaries.rend(); ++it) {
        if (it->second.nominal.leafCount == 1) continue;
        auto nominal = state.model->describe({it->first});
        if (!nominal.ok()) return Result<void>::failure(nominal.status());
        auto                             summary = unedited(nominal.value(), state.revision);
        std::array<EditedNodeSummary, 2> children;
        for (unsigned side = 0; side < 2; ++side) {
            const auto path  = it->first * 2 + side;
            const auto found = state.summaries.find(path);
            if (found != state.summaries.end())
                children[side] = found->second;
            else {
                auto child = state.model->describe({path});
                if (!child.ok()) return Result<void>::failure(child.status());
                children[side] = unedited(child.value(), state.revision);
            }
        }
        const auto& a        = children[0];
        const auto& b        = children[1];
        summary.massFraction = a.massFraction + b.massFraction;
        // Parallel-axis merge of central moments avoids subtracting large raw moments.
        constexpr std::array<unsigned, 6> row{0, 1, 2, 0, 0, 1};
        constexpr std::array<unsigned, 6> col{0, 1, 2, 1, 2, 2};
        const double weight = summary.massFraction > 0 ? a.massFraction * (b.massFraction / summary.massFraction) : 0;
        if (a.massFraction > 0 && b.massFraction > 0 && weight == 0)
            return fail<void>(DiagnosticCode::Unsupported, "Second moment merge weight underflows");
        for (unsigned k = 0; k < 6; ++k) {
            double shifted = 0;
            if (weight > 0) {
                const double dx = a.centerOfMass[row[k]] - b.centerOfMass[row[k]];
                const double dy = a.centerOfMass[col[k]] - b.centerOfMass[col[k]];
                shifted         = (weight * dx) * dy;
            }
            summary.centralSecondMomentPerRootMass[k] =
                a.centralSecondMomentPerRootMass[k] + b.centralSecondMomentPerRootMass[k] + shifted;
            if (!std::isfinite(summary.centralSecondMomentPerRootMass[k]))
                return fail<void>(DiagnosticCode::Unsupported, "Edited second moment exceeds local representation");
        }
        auto mass = assignMass(summary);
        if (!mass.ok()) return mass;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (summary.massFraction > 0)
                summary.centerOfMass[axis] = a.centerOfMass[axis] * (a.massFraction / summary.massFraction) +
                                             b.centerOfMass[axis] * (b.massFraction / summary.massFraction);
            summary.momentumPerRootMass[axis] = a.momentumPerRootMass[axis] + b.momentumPerRootMass[axis];
            summary.boundsMin[axis]           = std::min(a.boundsMin[axis], b.boundsMin[axis]);
            summary.boundsMax[axis]           = std::max(a.boundsMax[axis], b.boundsMax[axis]);
            if (!std::isfinite(summary.centerOfMass[axis]) || !std::isfinite(summary.momentumPerRootMass[axis]))
                return fail<void>(DiagnosticCode::Unsupported, "Edited moment exceeds local representation");
        }
        it->second = summary;
    }
    return Result<void>::success();
}
}  // namespace
struct PreparedModelUpdate::Impl {
    std::weak_ptr<const InstanceOwner> owner;
    Revision                           baseRevision;
    std::unique_ptr<State>             state;
};
struct RecursiveInstance::Impl {
    std::shared_ptr<const InstanceOwner> owner = std::make_shared<const InstanceOwner>();
    EditBudget                           budget;
    std::unique_ptr<State>               state;
};
PreparedModelUpdate::PreparedModelUpdate(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedModelUpdate::~PreparedModelUpdate()                                         = default;
PreparedModelUpdate::PreparedModelUpdate(PreparedModelUpdate&&) noexcept            = default;
PreparedModelUpdate& PreparedModelUpdate::operator=(PreparedModelUpdate&&) noexcept = default;
RecursiveInstance::RecursiveInstance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RecursiveInstance::~RecursiveInstance()                                       = default;
RecursiveInstance::RecursiveInstance(RecursiveInstance&&) noexcept            = default;
RecursiveInstance& RecursiveInstance::operator=(RecursiveInstance&&) noexcept = default;

Result<RecursiveInstance> RecursiveInstance::create(ModelDefinition definition, EditBudget budget) {
    if (budget.maxEdits == 0 || budget.maxEdits > 1000000 || budget.maxSummaries == 0 || budget.maxSummaries > 2000000)
        return fail<RecursiveInstance>(DiagnosticCode::InvalidArgument, "Invalid edit/summary staging limits");
    auto base = RecursiveModel::build(definition);
    if (!base.ok()) return Result<RecursiveInstance>::failure(base.status());
    try {
        auto impl               = std::make_unique<Impl>();
        impl->budget            = budget;
        impl->state             = std::make_unique<State>();
        impl->state->definition = definition;
        impl->state->revision   = definition.revision;
        impl->state->model      = std::make_shared<const RecursiveModel>(std::move(base.value()));
        return Result<RecursiveInstance>::success(RecursiveInstance(std::move(impl)));
    } catch (const std::bad_alloc&) {
        return fail<RecursiveInstance>(DiagnosticCode::Failed, "Instance allocation failed");
    }
}
Result<RecursiveInstance> RecursiveInstance::restoreState(const RecursiveState& state, EditBudget budget) {
    if (state.edits.size() > budget.maxEdits)
        return fail<RecursiveInstance>(DiagnosticCode::Failed, "Restore exceeds sparse edit budget");
    auto instance = create(state.definition, budget);
    if (!instance.ok()) return instance;
    try {
        auto& restored    = *instance.value().impl_->state;
        restored.revision = state.revision;
        restored.edits    = state.edits;
        auto compiled     = compileEdits(restored, budget);
        if (!compiled.ok()) return Result<RecursiveInstance>::failure(compiled.status());
        return instance;
    } catch (const std::bad_alloc&) {
        return fail<RecursiveInstance>(DiagnosticCode::Failed, "Restore allocation failed");
    }
}

Result<PreparedModelUpdate> RecursiveInstance::prepareEdits(std::span<const LeafEdit> completeEdits) const {
    if (!impl_) return fail<PreparedModelUpdate>(DiagnosticCode::StaleHandle, "Instance has been moved");
    if (completeEdits.size() > impl_->budget.maxEdits)
        return fail<PreparedModelUpdate>(DiagnosticCode::Failed, "Sparse edit budget exceeded");
    const auto next = impl_->state->revision.incremented();
    if (!next) return fail<PreparedModelUpdate>(DiagnosticCode::Unsupported, "Revision exhausted");
    try {
        auto candidate               = std::make_unique<PreparedModelUpdate::Impl>();
        candidate->owner             = impl_->owner;
        candidate->baseRevision      = impl_->state->revision;
        candidate->state             = std::make_unique<State>();
        candidate->state->definition = impl_->state->definition;
        candidate->state->model      = impl_->state->model;
        candidate->state->revision   = *next;
        candidate->state->edits.assign(completeEdits.begin(), completeEdits.end());
        auto compiled = compileEdits(*candidate->state, impl_->budget);
        if (!compiled.ok()) return Result<PreparedModelUpdate>::failure(compiled.status());
        return Result<PreparedModelUpdate>::success(PreparedModelUpdate(std::move(candidate)));
    } catch (const std::bad_alloc&) {
        return fail<PreparedModelUpdate>(DiagnosticCode::Failed, "Edit candidate allocation failed");
    }
}
Result<void> RecursiveInstance::commit(PreparedModelUpdate candidate) {
    if (!impl_ || !candidate.impl_ || candidate.impl_->owner.lock() != impl_->owner ||
        candidate.impl_->baseRevision != impl_->state->revision)
        return fail<void>(DiagnosticCode::StaleHandle, "Candidate is stale, foreign or already consumed");
    impl_->state.swap(candidate.impl_->state);
    return Result<void>::success();
}
Result<EditedNodeSummary> RecursiveInstance::describe(NodeKey key) const {
    if (!impl_) return fail<EditedNodeSummary>(DiagnosticCode::StaleHandle, "Instance has been moved");
    auto nominal = impl_->state->model->describe(key);
    if (!nominal.ok()) return Result<EditedNodeSummary>::failure(nominal.status());
    const auto found = impl_->state->summaries.find(key.path);
    return Result<EditedNodeSummary>::success(
        found == impl_->state->summaries.end() ? unedited(nominal.value(), impl_->state->revision) : found->second);
}
Result<std::vector<EditedNodeSummary>> RecursiveInstance::prepareFrontier(std::span<const NodeKey> keys,
                                                                          std::size_t              maxNodes) const {
    if (!impl_) return fail<std::vector<EditedNodeSummary>>(DiagnosticCode::StaleHandle, "Instance has been moved");
    auto coverage = impl_->state->model->prepareFrontier(keys, maxNodes);
    if (!coverage.ok()) return Result<std::vector<EditedNodeSummary>>::failure(coverage.status());
    try {
        std::vector<EditedNodeSummary> result;
        result.reserve(coverage.value().size());
        for (const auto& nominal : coverage.value()) {
            const auto found = impl_->state->summaries.find(nominal.key.path);
            result.push_back(found == impl_->state->summaries.end() ? unedited(nominal, impl_->state->revision)
                                                                    : found->second);
        }
        return Result<std::vector<EditedNodeSummary>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return fail<std::vector<EditedNodeSummary>>(DiagnosticCode::Failed, "Edited frontier allocation failed");
    }
}
Result<RecursiveState> RecursiveInstance::capture() const {
    if (!impl_) return fail<RecursiveState>(DiagnosticCode::StaleHandle, "Instance has been moved");
    try {
        return Result<RecursiveState>::success({impl_->state->definition, impl_->state->edits, impl_->state->revision});
    } catch (const std::bad_alloc&) {
        return fail<RecursiveState>(DiagnosticCode::Failed, "Capture allocation failed");
    }
}
}  // namespace eve::procgen::recursive
