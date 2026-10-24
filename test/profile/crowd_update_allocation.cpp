#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "crowd/Crowd.h"

namespace {
std::atomic<bool>        failOnce{false}, measure{false};
std::atomic<std::size_t> allocations{0};
}  // namespace
void* operator new(std::size_t size) {
    if (failOnce.exchange(false)) {
        throw std::bad_alloc();
    }
    if (measure.load()) ++allocations;
    if (auto* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }

int main() {
    using namespace eve;
    using namespace eve::crowd;
    Crowd crowd;
    crowd.setClampToField(false);
    const auto  reference = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    AgentUpdate update;
    update.reference = reference;
    update.data      = 17;
    AgentUpdateBatch batch;
    batch.agents.push_back(update);
    failOnce.store(true);
    auto failed = crowd.prepareAgentUpdates(batch);
    if (failed.ok() || crowd.getAgentState(0).data != 0) return 1;
    auto prepared = crowd.prepareAgentUpdates(batch);
    if (!prepared.ok()) return 2;
    if (!crowd.advance(CrowdStep{SimulationTick(1), Duration::fromNanoseconds(10000000), {}}).ok()) return 3;
    const std::uint8_t selection[] = {1};
    auto               token       = std::move(prepared).takeValue();
    allocations.store(0);
    measure.store(true);
    auto success       = Status::success();
    auto copiedSuccess = success;
    auto movedSuccess  = std::move(copiedSuccess);
    measure.store(false);
    if (allocations.load() != 0 || !movedSuccess.isSuccess() || !success.diagnostics().empty()) return 5;
    allocations.store(0);
    measure.store(true);
    auto committed = crowd.commitAgentUpdates(std::move(token), selection);
    measure.store(false);
    std::fprintf(stderr, "commit allocations: %zu; ok: %d\n", allocations.load(), int(committed.ok()));
    if (!committed.ok() || allocations.load() != 0 || crowd.getAgentState(0).data != 17) return 4;
    std::puts("update preparation failure is atomic; selected commit performs zero allocations");
    return 0;
}
