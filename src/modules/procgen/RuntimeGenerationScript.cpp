#include "procgen/RuntimeGenerationScript.h"
#include <charconv>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/RuntimeGeneration.h"

namespace eve::procgen {
namespace {
template <class T>
Result<T> procgenBindingFailure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.runtimeGeneration"));
}
}  // namespace
void exposeRuntimeGeneration(ssq::Table& table) {
    auto job = table.addClass<ProcgenGenerationJob>(
        "ProcgenGenerationJob", std::function<ProcgenGenerationJob*()>([] { return nullptr; }), true);
    job.addFunc("getLevel", &ProcgenGenerationJob::getLevel);
    job.addFunc("getX", &ProcgenGenerationJob::getX);
    job.addFunc("getZ", &ProcgenGenerationJob::getZ);
    job.addFunc("getSeed", &ProcgenGenerationJob::getSeed);
    job.addFunc("getTicket", &ProcgenGenerationJob::getTicket);
    job.addFunc("getMinX", &ProcgenGenerationJob::getMinX);
    job.addFunc("getMinZ", &ProcgenGenerationJob::getMinZ);
    job.addFunc("getMaxX", &ProcgenGenerationJob::getMaxX);
    job.addFunc("getMaxZ", &ProcgenGenerationJob::getMaxZ);
    auto cellRequest = table.addClass<ProcgenCellRequest>(
        "ProcgenCellRequest", std::function<ProcgenCellRequest*()>([]() -> ProcgenCellRequest* { return nullptr; }),
        true);
    cellRequest.addFunc("getLevel", &ProcgenCellRequest::getLevel);
    cellRequest.addFunc("getX", &ProcgenCellRequest::getX);
    cellRequest.addFunc("getZ", &ProcgenCellRequest::getZ);
    cellRequest.addFunc("getSeed", &ProcgenCellRequest::getSeed);
    cellRequest.addFunc("getTicket", &ProcgenCellRequest::getTicket);
    cellRequest.addFunc("getMinX", &ProcgenCellRequest::getMinX);
    cellRequest.addFunc("getMinZ", &ProcgenCellRequest::getMinZ);
    cellRequest.addFunc("getMaxX", &ProcgenCellRequest::getMaxX);
    cellRequest.addFunc("getMaxZ", &ProcgenCellRequest::getMaxZ);

    auto runtimeGeneration = table.addClass<RuntimeGeneration>(
        "ProcgenRuntimeGeneration", std::function<RuntimeGeneration*()>([]() -> RuntimeGeneration* { return nullptr; }),
        true);
    runtimeGeneration.addFunc("clear", &RuntimeGeneration::clear);
    runtimeGeneration.addFunc("addLevel", &RuntimeGeneration::addLevel);
    runtimeGeneration.addFunc("getLevelCount", &RuntimeGeneration::getLevelCount);
    runtimeGeneration.addFunc("getLevelCellSize", &RuntimeGeneration::getLevelCellSize);
    runtimeGeneration.addFunc("getLevelGenerationRadius", &RuntimeGeneration::getLevelGenerationRadius);
    runtimeGeneration.addFunc("getLevelCleanupRadius", &RuntimeGeneration::getLevelCleanupRadius);
    runtimeGeneration.addFunc("setDirectionWeight", &RuntimeGeneration::setDirectionWeight);
    runtimeGeneration.addFunc("getDirectionWeight", &RuntimeGeneration::getDirectionWeight);
    runtimeGeneration.addFunc("setMaxGenerating", &RuntimeGeneration::setMaxGenerating);
    runtimeGeneration.addFunc("getMaxGenerating", &RuntimeGeneration::getMaxGenerating);
    runtimeGeneration.addFunc("setMaxActiveCells", &RuntimeGeneration::setMaxActiveCells);
    runtimeGeneration.addFunc("getMaxActiveCells", &RuntimeGeneration::getMaxActiveCells);
    runtimeGeneration.addFunc("setMaxPointsPerCell", &RuntimeGeneration::setMaxPointsPerCell);
    runtimeGeneration.addFunc("getMaxPointsPerCell", &RuntimeGeneration::getMaxPointsPerCell);
    runtimeGeneration.addFunc("setMaxResidentPoints", &RuntimeGeneration::setMaxResidentPoints);
    runtimeGeneration.addFunc("getMaxResidentPoints", &RuntimeGeneration::getMaxResidentPoints);
    runtimeGeneration.addFunc("getResidentPointCount", &RuntimeGeneration::getResidentPointCount);
    runtimeGeneration.addFunc("getRejectedOutputCount", &RuntimeGeneration::getRejectedOutputCount);
    runtimeGeneration.addFunc("trimToResidentPoints", &RuntimeGeneration::trimToResidentPoints);
    runtimeGeneration.addFunc("setMaxGenerationRetries", &RuntimeGeneration::setMaxGenerationRetries);
    runtimeGeneration.addFunc("getMaxGenerationRetries", &RuntimeGeneration::getMaxGenerationRetries);
    runtimeGeneration.addFunc("setFrameTimeBudget", &RuntimeGeneration::setFrameTimeBudget);
    runtimeGeneration.addFunc("getFrameTimeBudget", &RuntimeGeneration::getFrameTimeBudget);
    runtimeGeneration.addFunc("beginFrame", &RuntimeGeneration::beginFrame);
    runtimeGeneration.addFunc("setRefreshWorkBudget", &RuntimeGeneration::setRefreshWorkBudget);
    runtimeGeneration.addFunc("getRefreshWorkBudget", &RuntimeGeneration::getRefreshWorkBudget);
    runtimeGeneration.addFunc("isRefreshPending", &RuntimeGeneration::isRefreshPending);
    runtimeGeneration.addFunc("getCommittedRefreshRevision", &RuntimeGeneration::getCommittedRefreshRevision);
    runtimeGeneration.addFunc(
        "continueGenerationRefresh", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value) {
            return eve::script::projectResult(
                vm,
                value ? value->continueGenerationRefresh()
                      : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                             "continueGenerationRefresh requires RuntimeGeneration",
                                                             "runtimeGeneration"),
                [](std::uint64_t processed) { return eve::Value(std::to_string(processed)); });
        });
    runtimeGeneration.addFunc("updateSource", &RuntimeGeneration::updateSource);
    runtimeGeneration.addFunc("setGenerationSource", &RuntimeGeneration::setGenerationSource);
    runtimeGeneration.addFunc("removeGenerationSource", &RuntimeGeneration::removeGenerationSource);
    runtimeGeneration.addFunc("clearGenerationSources", &RuntimeGeneration::clearGenerationSources);
    runtimeGeneration.addFunc("getGenerationSourceCount", &RuntimeGeneration::getGenerationSourceCount);
    runtimeGeneration.addFunc("getGenerationSourceId", &RuntimeGeneration::getGenerationSourceId);
    runtimeGeneration.addFunc("refreshGenerationSources", &RuntimeGeneration::refreshGenerationSources);
    runtimeGeneration.addFunc("setFrustumCulling", &RuntimeGeneration::setFrustumCulling);
    runtimeGeneration.addFunc("isFrustumCullingEnabled", &RuntimeGeneration::isFrustumCullingEnabled);
    runtimeGeneration.addFunc("getFrustumHalfAngle", &RuntimeGeneration::getFrustumHalfAngle);
    runtimeGeneration.addFunc("getFrustumBehindRadius", &RuntimeGeneration::getFrustumBehindRadius);
    runtimeGeneration.addFunc("getPendingGenerateCount", &RuntimeGeneration::getPendingGenerateCount);
    runtimeGeneration.addFunc("getGeneratingCount", &RuntimeGeneration::getGeneratingCount);
    runtimeGeneration.addFunc("getActiveCellCount", &RuntimeGeneration::getActiveCellCount);
    runtimeGeneration.addFunc("getPendingCleanupCount", &RuntimeGeneration::getPendingCleanupCount);
    runtimeGeneration.addFunc("getCancelledGenerationCount", &RuntimeGeneration::getCancelledGenerationCount);
    runtimeGeneration.addFunc("getFailedCellCount", &RuntimeGeneration::getFailedCellCount);
    runtimeGeneration.addFunc("retryFailedCells", &RuntimeGeneration::retryFailedCells);
    runtimeGeneration.addFunc("nextGenerate", &RuntimeGeneration::nextGenerate);
    runtimeGeneration.addFunc("nextGenerationJob", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* self) {
        auto object =
            eve::script::makeOwnedSquirrelInstance<ProcgenGenerationJob>(vm, std::make_unique<ProcgenGenerationJob>());
        if (!object.ok()) return eve::script::projectStatusResult(vm, object.status(), false, false);
        auto result = self->nextGenerationJob();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto value     = std::move(result).takeValue();
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        if (!value) return projected;
        auto owned                         = std::move(object).takeValue();
        *owned.to<ProcgenGenerationJob*>() = *value;
        projected.set("value", owned);
        return projected;
    });
    runtimeGeneration.addFunc(
        "completeGenerationJob",
        [vm = runtimeGeneration.getHandle()](RuntimeGeneration* self, ProcgenGenerationJob* job, PointSet* output) {
            return eve::script::projectResult(
                vm,
                job && output ? self->completeGenerationJob({*job, *output})
                              : procgenBindingFailure<uint64_t>(DiagnosticCode::InvalidArgument,
                                                                "job and output are required", "completion"),
                [](uint64_t revision) { return Value(std::to_string(revision)); });
        });
    runtimeGeneration.addFunc(
        "failGenerationJob", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* self, ProcgenGenerationJob* job) {
            return eve::script::projectResult(
                vm, job ? self->failGenerationJob(*job)
                        : procgenBindingFailure<void>(DiagnosticCode::InvalidArgument, "job is required", "job"));
        });
    runtimeGeneration.addFunc("nextCleanupRequest", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* self) {
        // Allocate the registered script wrapper before consuming a queue entry.
        auto object =
            eve::script::makeOwnedSquirrelInstance<ProcgenCellRequest>(vm, std::make_unique<ProcgenCellRequest>());
        if (!object.ok()) return eve::script::projectStatusResult(vm, object.status(), false, false);
        auto result = self->nextCleanupRequest();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto value     = std::move(result).takeValue();
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        if (!value) return projected;
        auto owned                       = std::move(object).takeValue();
        *owned.to<ProcgenCellRequest*>() = *value;
        projected.set("value", owned);
        return projected;
    });
    runtimeGeneration.addFunc("completeCleanupRequest", [vm = runtimeGeneration.getHandle()](
                                                            RuntimeGeneration* self, ProcgenCellRequest* request) {
        return eve::script::projectResult(
            vm,
            request ? self->completeCleanupRequest(*request)
                    : procgenBindingFailure<uint64_t>(DiagnosticCode::InvalidArgument, "cleanup request is required",
                                                      "request"),
            [](uint64_t count) { return Value(std::to_string(count)); });
    });
    runtimeGeneration.addFunc("nextCleanup", &RuntimeGeneration::nextCleanup);
    runtimeGeneration.addFunc("isRequestCurrent", &RuntimeGeneration::isRequestCurrent);
    runtimeGeneration.addFunc("completeGeneration", &RuntimeGeneration::completeGeneration);
    runtimeGeneration.addFunc("failGeneration", &RuntimeGeneration::failGeneration);
    runtimeGeneration.addFunc("completeCleanup", &RuntimeGeneration::completeCleanup);
    runtimeGeneration.addFunc("completeCleanupsAtomic", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value,
                                                                                             ssq::Array requestArray) {
        std::vector<const ProcgenCellRequest*> requests;
        requests.reserve(requestArray.size());
        for (size_t index = 0; index < requestArray.size(); ++index)
            requests.push_back(requestArray.get<ProcgenCellRequest*>(index));
        return eve::script::projectResult(
            vm,
            value
                ? value->completeCleanupsAtomic(requests)
                : procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                       "completeCleanupsAtomic requires RuntimeGeneration", "runtime"),
            [](std::uint64_t completed) { return eve::Value(std::to_string(completed)); });
    });
    runtimeGeneration.addFunc("hasCell", &RuntimeGeneration::hasCell);
    runtimeGeneration.addFunc("getCellOutput", &RuntimeGeneration::getCellOutput);
    runtimeGeneration.addFunc("getCellRevision", &RuntimeGeneration::getCellRevision);
    runtimeGeneration.addFunc(
        "applyCellUpdate", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value, int level, int x, int z,
                                                                const std::string& revisionText, PointSet* output) {
            std::uint64_t revision = 0;
            const auto [end, error] =
                std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), revision);
            if (!output || error != std::errc{} || end != revisionText.data() + revisionText.size() || revision == 0)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<std::uint64_t>(
                        eve::DiagnosticCode::InvalidArgument,
                        "applyCellUpdate requires an output and a non-zero decimal revision", "revision"),
                    [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
            return eve::script::projectResult(
                vm, value->applyCellUpdate(level, x, z, revision, *output),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        });
    runtimeGeneration.addFunc(
        "migrateCellPointIds", [vm = runtimeGeneration.getHandle()](RuntimeGeneration* value, int level, int x, int z,
                                                                    const std::string& revisionText) {
            std::uint64_t revision = 0;
            const auto [end, error] =
                std::from_chars(revisionText.data(), revisionText.data() + revisionText.size(), revision);
            if (error != std::errc{} || end != revisionText.data() + revisionText.size() || revision == 0)
                return eve::script::projectResult(
                    vm,
                    procgenBindingFailure<std::uint64_t>(eve::DiagnosticCode::InvalidArgument,
                                                         "migrateCellPointIds requires a non-zero decimal revision",
                                                         "revision"),
                    [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
            return eve::script::projectResult(
                vm, value->migrateCellPointIds(level, x, z, revision),
                [](std::uint64_t committed) { return eve::Value(std::to_string(committed)); });
        });
    runtimeGeneration.addFunc("getCellDelta", &RuntimeGeneration::getCellDelta);
    runtimeGeneration.addFunc("serializeCell", &RuntimeGeneration::serializeCell);
    runtimeGeneration.addFunc("deserializeCell", &RuntimeGeneration::deserializeCell);
    runtimeGeneration.addFunc("debugReport", &RuntimeGeneration::debugReport);
}
}  // namespace eve::procgen
