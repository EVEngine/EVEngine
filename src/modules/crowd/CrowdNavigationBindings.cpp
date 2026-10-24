#include <functional>
#include <limits>
#include "common/SquirrelBinding.h"
#include "crowd/CrowdInternal.h"

namespace eve::crowd {
namespace {
ssq::Object rawRequestField(const ssq::Table& token, const char* key) {
    auto        vm  = token.getHandle();
    const auto  top = sq_gettop(vm);
    ssq::Object result(vm);
    sq_pushobject(vm, token.getRaw());
    sq_pushstring(vm, key, -1);
    if (SQ_FAILED(sq_rawget(vm, -2))) {
        sq_settop(vm, top);
        throw ssq::NotFoundException(key);
    }
    if (SQ_FAILED(sq_getstackobj(vm, -1, &result.getRaw()))) {
        sq_settop(vm, top);
        throw ssq::TypeException("Cannot read raw request field");
    }
    sq_addref(vm, &result.getRaw());
    sq_settop(vm, top);
    return result;
}
std::int64_t requestInteger(const ssq::Table& token, const char* key) {
    const auto value = rawRequestField(token, key);
    if (value.getType() != ssq::Type::INTEGER) throw ssq::TypeException("Flow token requires integer fields");
    return value.to<std::int64_t>();
}
Value projectFlowProgress(Crowd::FlowBuildProgress progress) {
    constexpr const char* states[] = {"ready", "cancelled", "committed", "pending", "failed", "unreachable"};
    return Value(Value::Object{{"state", Value(std::string(states[static_cast<int>(progress.state)]))},
                               {"heapPops", Value(progress.heapPops)},
                               {"directionCells", Value(progress.directionCells)}});
}
}  // namespace
void Crowd::clearNavigationRequests() noexcept { navigationRequests_.reset(); }

Result<std::reference_wrapper<Crowd::NavigationRequests::Entry>> Crowd::NavigationRequests::resolve(
    std::uint64_t currentOwner, const ssq::Table& token) {
    try {
        if (rawRequestField(token, "kind").to<std::string>() != "crowd.flowRequest" ||
            requestInteger(token, "schema") != 1)
            throw ssq::TypeException("Unknown flow token kind or schema");
        const auto owner = requestInteger(token, "owner"), index = requestInteger(token, "index"),
                   generation = requestInteger(token, "generation");
        if (owner > 0 && static_cast<std::uint64_t>(owner) == currentOwner && index >= 0 &&
            static_cast<std::uint64_t>(index) < entries.size()) {
            auto& entry = entries[static_cast<std::size_t>(index)];
            if (entry.occupied && generation > 0 && static_cast<std::uint64_t>(generation) == entry.generation)
                return Result<std::reference_wrapper<Entry>>::success(std::ref(entry));
        }
        return Result<std::reference_wrapper<Entry>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Stale or foreign flow request", "flowRequest"));
    } catch (const std::exception& error) {
        return Result<std::reference_wrapper<Entry>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "flowRequest"));
    }
}
Result<FlowLease> resolveFlowRequestLease(Crowd& crowd, const ssq::Table& token) {
    if (!crowd.navigationRequests_)
        return Result<FlowLease>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "No flow request registry", "flowRequest"));
    auto found = crowd.navigationRequests_->resolve(crowd.impl_->owner, token);
    if (!found) return Result<FlowLease>::failure(found.status());
    const auto& lease = found.value().get().lease;
    if (!lease || !crowd.impl_->isFlowLeaseCurrent(*lease))
        return Result<FlowLease>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Missing or stale request lease", "flowRequest"));
    return Result<FlowLease>::success(*lease);
}
void exposeNavigationRequests(ssq::Class& cls) {
    using Entry       = Crowd::NavigationRequests::Entry;
    const auto lookup = [](Crowd* crowd, ssq::Table token) -> Result<std::reference_wrapper<Entry>> {
        if (!crowd->navigationRequests_)
            return Result<std::reference_wrapper<Entry>>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "No flow request registry", "flowRequest"));
        return crowd->navigationRequests_->resolve(crowd->impl_->owner, token);
    };
    cls.addFunc("requestFlowBuild", [vm = cls.getHandle()](Crowd* crowd, int gx, int gy) {
        const auto begin = [&]() -> ssq::Table {
            try {
                if (!crowd->navigationRequests_)
                    crowd->navigationRequests_ = std::make_unique<Crowd::NavigationRequests>();
                auto&       entries = crowd->navigationRequests_->entries;
                std::size_t index   = 0;
                while (index < entries.size() && (entries[index].occupied || entries[index].generation == UINT32_MAX))
                    ++index;
                if (index == 64)
                    return script::projectResult(vm,
                                                 Result<Value>::failure(Diagnostic::error(
                                                     DiagnosticCode::PreconditionViolation,
                                                     "Flow request registry capacity exhausted", "flowRequest")),
                                                 [](Value value) { return value; });
                auto prepared = crowd->prepareFlowBuild(gx, gy);
                if (!prepared)
                    return script::projectResult(vm, Result<Value>::failure(prepared.status()),
                                                 [](Value value) { return value; });
                const auto generation = index < entries.size() ? entries[index].generation : 1;
                Value      token(Value::Object{{"kind", Value(std::string("crowd.flowRequest"))},
                                               {"schema", Value(1)},
                                               {"owner", Value(static_cast<std::int64_t>(crowd->impl_->owner))},
                                               {"index", Value(static_cast<std::int64_t>(index))},
                                               {"generation", Value(static_cast<std::int64_t>(generation))}});
                entries.reserve(index + 1);
                auto response = script::projectResult(vm, Result<Value>::success(std::move(token)),
                                                      [](Value value) { return value; });
                if (index == entries.size()) entries.emplace_back();
                entries[index].plan     = std::move(prepared).takeValue();
                entries[index].occupied = true;
                return response;
            } catch (const std::exception& error) {
                return script::projectResult(
                    vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "flowRequest")),
                    [](Value value) { return value; });
            }
        };
        return begin();
    });
    cls.addFunc("acquireFlowRequestLease", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token) {
        auto found = lookup(crowd, token);
        if (!found)
            return script::projectResult(vm, Result<Value>::failure(found.status()), [](Value value) { return value; });
        auto& entry = found.value().get();
        if (entry.lease)
            return script::projectResult(vm,
                                         Result<Value>::failure(Diagnostic::error(
                                             DiagnosticCode::Conflict, "Request already owns a lease", "flowRequest")),
                                         [](Value value) { return value; });
        // Pin belongs to the already known request; callers can release it even if VM projection fails.
        auto lease = crowd->acquireFlowField(std::move(entry.plan));
        if (!lease)
            return script::projectResult(vm, Result<Value>::failure(lease.status()), [](Value value) { return value; });
        entry.lease = std::move(lease).takeValue();
        return script::projectResult(vm,
                                     Result<Value>::success(Value(Value::Object{
                                         {"generation", Value(std::to_string(entry.lease->generation()))}})),
                                     [](Value value) { return value; });
    });
    cls.addFunc("sampleFlowRequestLease", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token, float x,
                                                                         float y) {
        auto found = lookup(crowd, token);
        if (!found)
            return script::projectResult(vm, Result<FlowSample>::failure(found.status()),
                                         [](FlowSample) { return Value{}; });
        auto& entry = found.value().get();
        if (!entry.lease)
            return script::projectResult(vm,
                                         Result<FlowSample>::failure(Diagnostic::error(
                                             DiagnosticCode::Conflict, "Request has no acquired lease", "flowRequest")),
                                         [](FlowSample) { return Value{}; });
        return script::projectResult(vm, crowd->sampleFlowField(*entry.lease, x, y), [](FlowSample value) {
            return Value(Value::Object{
                {"state", Value(std::string(value.state == FlowSampleState::Reachable ? "reachable" : "unreachable"))},
                {"x", Value(value.direction.x)},
                {"y", Value(value.direction.y)}});
        });
    });
    cls.addFunc("advanceFlowRequest", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token, int maxWork) {
        auto entry = lookup(crowd, token);
        if (!entry)
            return script::projectResult(vm, Result<Crowd::FlowBuildProgress>::failure(entry.status()),
                                         projectFlowProgress);
        return script::projectResult(vm, crowd->advanceFlowBuild(entry.value().get().plan, maxWork),
                                     projectFlowProgress);
    });
    cls.addFunc("getFlowRequest", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token) {
        auto entry = lookup(crowd, token);
        if (!entry)
            return script::projectResult(vm, Result<Crowd::FlowBuildProgress>::failure(entry.status()),
                                         projectFlowProgress);
        auto& plan = entry.value().get().plan;
        if (plan.state() == Crowd::FlowBuildState::Pending || plan.state() == Crowd::FlowBuildState::Ready ||
            plan.state() == Crowd::FlowBuildState::Unreachable)
            return script::projectResult(vm, crowd->advanceFlowBuild(plan, 0), projectFlowProgress);
        return script::projectResult(vm, Result<Crowd::FlowBuildProgress>::success({plan.state(), 0, 0}),
                                     projectFlowProgress);
    });
    cls.addFunc("commitFlowRequest", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token) {
        auto entry = lookup(crowd, token);
        if (!entry) return script::projectResult(vm, Result<void>::failure(entry.status()));
        return script::projectResult(vm, crowd->commitFlowBuild(std::move(entry.value().get().plan)));
    });
    cls.addFunc("cancelFlowRequest", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token) {
        auto entry = lookup(crowd, token);
        if (!entry) return script::projectResult(vm, Result<void>::failure(entry.status()));
        return script::projectResult(vm, entry.value().get().plan.cancel());
    });
    cls.addFunc("releaseFlowRequest", [vm = cls.getHandle(), lookup](Crowd* crowd, ssq::Table token) {
        auto found = lookup(crowd, token);
        if (!found) return script::projectResult(vm, Result<void>::failure(found.status()));
        auto& entry = found.value().get();
        entry.lease.reset();
        entry.plan     = Crowd::FlowBuildPlan{};
        entry.occupied = false;
        if (entry.generation != UINT32_MAX) ++entry.generation;
        return script::projectResult(vm, Result<void>::success(Status::success(StatusCode::Applied)));
    });
}
}  // namespace eve::crowd
