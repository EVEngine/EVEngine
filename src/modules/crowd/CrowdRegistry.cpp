#include "crowd/CrowdInternal.h"

#include "common/Exception.h"

#include <atomic>
#include <limits>

namespace eve::crowd {
namespace {
std::uint64_t allocateOwner() {
    static std::atomic<std::uint64_t> next{1};
    auto                              value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            throw eve::Exception("Crowd owner identity exhausted");
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    }
}
}  // namespace

Crowd::Impl::Impl() : owner(allocateOwner()) {}

std::uint64_t namedTieValue(std::string_view identity) {
    std::uint64_t tie = 1469598103934665603ULL;
    for (const unsigned char value : identity) {
        tie ^= value;
        tie *= 1099511628211ULL;
    }
    return tie;
}

void Crowd::Impl::reserveAgentAppend() {
    const auto needed  = xs.size() + 1;
    const auto target  = std::min(std::size_t(maxAgents), std::max(needed, xs.capacity() * 2));
    const auto reserve = [target](auto& values) {
        if (values.capacity() < target) values.reserve(target);
    };
    reserve(xs);
    reserve(ys);
    reserve(headings);
    reserve(vxs);
    reserve(vys);
    reserve(speeds);
    reserve(radii);
    reserve(maxSpeeds);
    reserve(maxAccels);
    reserve(turnRates);
    reserve(actions);
    reserve(datas);
    reserve(avoidancePriorities);
    reserve(hasTargets);
    reserve(interactions);
    reserve(movements);
    reserve(flows);
    reserve(targetXs);
    reserve(targetYs);
    reserve(wanderPhases);
    reserve(stableIds);
    reserve(tieKeys);
    reserve(denseToRegistry);
    if (freeRegistry.capacity() < registry.size() + 1)
        freeRegistry.reserve(std::max(registry.size() + 1, freeRegistry.capacity() * 2));
    if (freeRegistry.empty()) {
        const auto count = registry.size() + 1;
        if (count >= std::size_t(AgentHandle::invalidIndex)) throw eve::Exception("Crowd registry identity exhausted");
        const auto capacity = std::max(count, registry.capacity() * 2);
        if (registry.capacity() < count) registry.reserve(capacity);
        if (freeRegistry.capacity() < count) freeRegistry.reserve(capacity);
    }
}

void Crowd::Impl::appendAgentIdentity(int denseIndex) {
    std::uint32_t index;
    if (freeRegistry.empty()) {
        index = static_cast<std::uint32_t>(registry.size());
        registry.push_back({1, denseIndex});
    } else {
        index = freeRegistry.back();
        freeRegistry.pop_back();
        registry[index].denseIndex = denseIndex;
    }
    denseToRegistry.push_back(index);
}

void Crowd::Impl::eraseAgentIdentity(int denseIndex) {
    const auto index      = denseToRegistry[std::size_t(denseIndex)];
    auto&      entry      = registry[index];
    entry.denseIndex      = -1;
    const auto generation = AgentHandle::nextGeneration(entry.generation);
    if (generation) {
        entry.generation = *generation;
        freeRegistry.push_back(index);
    }
    if (std::size_t(denseIndex) != denseToRegistry.size() - 1) {
        const auto moved                         = denseToRegistry.back();
        denseToRegistry[std::size_t(denseIndex)] = moved;
        registry[moved].denseIndex               = denseIndex;
    }
    denseToRegistry.pop_back();
}

void Crowd::Impl::clearAgentIdentities() {
    while (!denseToRegistry.empty()) eraseAgentIdentity(static_cast<int>(denseToRegistry.size() - 1));
}

int Crowd::Impl::resolveAgent(AgentRef reference) const noexcept {
    if (reference.owner != owner || !reference.handle.isValid() || reference.handle.index() >= registry.size())
        return -1;
    const auto& entry = registry[reference.handle.index()];
    return entry.generation == reference.handle.generation() ? entry.denseIndex : -1;
}

Result<AgentRef> Crowd::agentRef(int slot) const {
    if (!impl_->validId(slot))
        return Result<AgentRef>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "slot"));
    const auto index = impl_->denseToRegistry[std::size_t(slot)];
    return Result<AgentRef>::success({AgentHandle(index, impl_->registry[index].generation), impl_->owner});
}

Result<AgentRef> Crowd::resolveNamedAgent(const std::string& stableId) const {
    return agentRef(getNamedAgentIndex(stableId));
}

bool Crowd::isAgentAlive(AgentRef reference) const noexcept { return impl_->resolveAgent(reference) >= 0; }

Result<AgentState> Crowd::readAgent(AgentRef reference) const {
    const int slot = impl_->resolveAgent(reference);
    if (slot < 0)
        return Result<AgentState>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Crowd agent reference is stale or belongs to another owner", "reference"));
    return Result<AgentState>::success(getAgentState(slot));
}

}  // namespace eve::crowd
