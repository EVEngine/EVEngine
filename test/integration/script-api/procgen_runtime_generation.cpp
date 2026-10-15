#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <utility>

using namespace eve::procgen;

namespace {

/** @brief Owns a module-backed runtime-generation handle for one test. */
class RuntimeLease {
public:
    RuntimeLease() = default;
    RuntimeLease(Procgen& owner, ProcgenRuntimeGenerationHandleRef handle) : owner_(&owner), handle_(handle) {}

    RuntimeLease(const RuntimeLease&)            = delete;
    RuntimeLease& operator=(const RuntimeLease&) = delete;
    RuntimeLease(RuntimeLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    RuntimeLease& operator=(RuntimeLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~RuntimeLease() { reset(); }

    /** @brief Releases the scheduler handle and explicitly observes the Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->release(handle_);
        result.ignore("test runtime-generation cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Provides a short-lived borrowed view of the scheduler. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> view() const noexcept {
        return owner_ ? owner_->resolveRuntimeGeneration(handle_) : eve::script::Borrowed<RuntimeGeneration>();
    }

    /** @brief Preserves the concise `runtime->operation()` test syntax. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> operator->() const noexcept { return view(); }

private:
    Procgen*                          owner_ = nullptr;
    ProcgenRuntimeGenerationHandleRef handle_{};
};

/** @brief Converts a checked scheduler allocation Result into a test lease. */
[[maybe_unused]] RuntimeLease requireRuntime(Procgen& proc, uint32_t worldSeed) {
    auto       result = proc.newRuntimeGenerationHandle(worldSeed);
    const bool ok     = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return RuntimeLease(proc, std::move(result).takeValue());
}

/** @brief Gives an owning lifetime to a request allocated by the scheduler API. */
using RequestLease = std::unique_ptr<ProcgenCellRequest>;

[[nodiscard]] RequestLease ownRequest(ProcgenCellRequest* request) noexcept { return RequestLease(request); }

}  // namespace


TEST_CASE("procgen.runtimeGeneration.squirrelCompletesCleanupTicketsAtomically") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(81);
        local pointsResult = procgen.sampleGrid(1, 1, 1.0, 18, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 16.0, 1.5);
            runtime.setMaxGenerating(8);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local first = runtime.nextGenerate();
            local second = runtime.nextGenerate();
            if (first != null && second != null && runtime.completeGeneration(first, points) &&
                runtime.completeGeneration(second, points)) {
                runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
                local firstCleanup = runtime.nextCleanup();
                local secondCleanup = runtime.nextCleanup();
                local completed = runtime.completeCleanupsAtomic([firstCleanup, secondCleanup]);
                if (completed.ok && completed.value == "2" &&
                    !runtime.isRequestCurrent(firstCleanup) && !runtime.isRequestCurrent(secondCleanup))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


TEST_CASE("procgen.runtimeGeneration.squirrelAppliesAndInspectsCellDelta") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(934);
        local pointsResult = procgen.sampleGrid(2, 1, 1.0, 17, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 6.0, 1.5);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local request = runtime.nextGenerate();
            if (request != null && runtime.completeGeneration(request, points)) {
                points.setPosition(0, 9.0, 2.0, -3.0);
                local update = runtime.applyCellUpdate(
                    request.getLevel(), request.getX(), request.getZ(), "1", points);
                local delta = runtime.getCellDelta(request.getLevel(), request.getX(), request.getZ());
                if (update.ok && update.value == "2" && delta != null &&
                    delta.getAddedCount() == 0 && delta.getUpdatedCount() == 1 &&
                    delta.getRemovedCount() == 0 && delta.getTargetCount() == 2 &&
                    delta.getBaseFingerprint() != delta.getTargetFingerprint() &&
                    delta.getUpdated().getPointId(0) == points.getPointId(0))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


TEST_CASE("procgen.runtimeGeneration.squirrelAdvancesBudgetedRefresh") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local created = procgen.newRuntimeGeneration(1704);
        if (created.ok) {
            local runtime = created.value;
            runtime.addLevel(10.0, 25.0, 1.5);
            runtime.setRefreshWorkBudget(2);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local total = 0;
            while (runtime.isRefreshPending()) {
                local advanced = runtime.continueGenerationRefresh();
                if (!advanced.ok) break;
                total += advanced.value.tointeger();
            }
            if (runtime.getRefreshWorkBudget() == 2 &&
                runtime.getCommittedRefreshRevision() == 1 &&
                runtime.getPendingGenerateCount() > 0 && total > 0)
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
