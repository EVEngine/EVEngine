#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// procgen_scene_sink.cpp
using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const auto                            previous = batches.find(batchId);
        std::unordered_map<std::string, bool> oldIds;
        if (previous != batches.end())
            for (const auto& instance : previous->second) oldIds[instance.id] = true;
        Stats stats;
        for (const auto& instance : instances) {
            if (oldIds.erase(instance.id) != 0)
                ++stats.reused;
            else
                ++stats.created;
        }
        stats.removed      = int(oldIds.size());
        batches[batchId]   = instances;
        revisions[batchId] = revisions[batchId] + 1;
        latest[batchId]    = stats;
        return true;
    }
    eve::Result<uint64_t> replaceBatch(const std::string& batchId, uint64_t targetRevision,
                                       const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        if (targetRevision == 0 || batchRevision(batchId) >= targetRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock snapshot revision is stale"));
        if (!applyBatch(batchId, instances))
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "mock snapshot commit failed"));
        ++replaceCalls;
        revisions[batchId] = targetRevision;
        return eve::Result<uint64_t>::success(targetRevision);
    }
    eve::Result<uint64_t> replaceBatches(const std::vector<eve::ProcgenBatchSnapshot>& snapshots) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || snapshot.targetRevision == 0 ||
                nextRevisions[snapshot.batchId] >= snapshot.targetRevision)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock batch transaction is stale"));
            nextBatches[snapshot.batchId]   = snapshot.instances;
            nextRevisions[snapshot.batchId] = snapshot.targetRevision;
        }
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(snapshots.size()));
    }
    eve::Result<uint64_t> applyDelta(const std::string& batchId, const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions.find(batchId);
        if (current == revisions.end() || current->second != delta.baseRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock delta revision is stale"));
        lastDeltaBatch = batchId;
        lastDelta      = delta;
        ++deltaCalls;
        revisions[batchId] = delta.targetRevision;
        return eve::Result<uint64_t>::success(delta.targetRevision);
    }
    uint64_t batchRevision(const std::string& batchId) const override {
        const auto found = revisions.find(batchId);
        return found == revisions.end() ? 0 : found->second;
    }
    bool removeBatch(const std::string& batchId) override {
        const auto found = batches.find(batchId);
        if (found == batches.end()) return false;
        latest[batchId] = {0, 0, int(found->second.size())};
        batches.erase(found);
        revisions.erase(batchId);
        return true;
    }
    eve::Result<uint64_t> removeBatches(const std::vector<std::string>& batchIds) override {
        return removeBatchesCoordinated(batchIds, [] { return eve::Result<void>::success(); });
    }
    eve::Result<uint64_t> removeBatchesCoordinated(
        const std::vector<std::string>&           batchIds,
        const std::function<eve::Result<void>()>& commitPreparedOwner) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& batchId : batchIds) {
            if (nextBatches.erase(batchId) != 1)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "mock batch is unpublished"));
            nextRevisions.erase(batchId);
        }
        if (!commitPreparedOwner)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "mock commit is missing"));
        auto ownerCommit = commitPreparedOwner();
        if (!ownerCommit.ok()) return eve::Result<uint64_t>::failure(ownerCommit.status());
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(batchIds.size()));
    }
    int instanceCount(const std::string& batchId) const override {
        const auto found = batches.find(batchId);
        return found == batches.end() ? 0 : int(found->second.size());
    }
    int lastCreatedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.created;
    }
    int lastReusedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.reused;
    }
    int lastRemovedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.removed;
    }

    struct Stats {
        int created = 0;
        int reused  = 0;
        int removed = 0;
    };
    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> batches;
    std::unordered_map<std::string, uint64_t>                              revisions;
    std::unordered_map<std::string, Stats>                                 latest;
    std::string                                                            lastDeltaBatch;
    eve::ProcgenInstanceDelta                                              lastDelta;
    int                                                                    replaceCalls         = 0;
    int                                                                    deltaCalls           = 0;
    bool                                                                   failBatchTransaction = false;
};

}  // namespace


TEST_CASE("procgen.sceneSink.squirrelCompletesCoordinatedCleanup") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(33);
        local pointsResult = procgen.sampleGrid(1, 1, 1.0, 34, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            runtime.addLevel(10.0, 6.0, 1.5);
            runtime.updateSource(1.0, 1.0, 1.0, 0.0);
            local generated = runtime.nextGenerate();
            if (generated != null && runtime.completeGeneration(generated, pointsResult.value)) {
                local published = procgen.synchronizeCellInstances(
                    "script-cleanup", runtime, generated, "asset", "stone");
                runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
                local cleanup = runtime.nextCleanup();
                local completed = procgen.completeCellCleanupAtomic(
                    "script-cleanup", [runtime], [cleanup]);
                if (published.ok && completed.ok && completed.value == "1" &&
                    !runtime.isRequestCurrent(cleanup))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    CHECK(sink.batches.empty());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}


// procgen_schema_script.cpp
TEST_CASE("procgen.script.reflects_schema_and_generates_from_dynamic_values") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        algorithm <- "cave.cellular";
        paramsResult <- procgen.newParams();
        paramsResultOk <- paramsResult.ok;
        params <- paramsResult.ok ? paramsResult.value : null;
        defaultsResult <- params == null ? { ok = false } :
            procgen.applyAlgorithmDefaults(algorithm, params);
        defaultsApplied <- defaultsResult.ok;
        paramCount <- procgen.getAlgorithmParamCount(algorithm);
        fieldsComplete <- true;
        fillIndex <- -1;
        for (local i = 0; i < paramCount; ++i) {
            if (procgen.getAlgorithmParamKey(algorithm, i) == "" ||
                procgen.getAlgorithmParamLabel(algorithm, i) == "" ||
                procgen.getAlgorithmParamKind(algorithm, i) == "") fieldsComplete = false;
            if (procgen.getAlgorithmParamKey(algorithm, i) == "fill") fillIndex = i;
        }
        widthSet <- params == null ? { ok = false } : params.setInt("width", 40);
        heightSet <- params == null ? { ok = false } : params.setInt("height", 24);
        seedSet <- params == null ? { ok = false } : params.setInt("seed", 77);
        fillSet <- params == null ? { ok = false } : params.setFloat("fill", 0.38);
        widthSetOk <- widthSet.ok;
        heightSetOk <- heightSet.ok;
        seedSetOk <- seedSet.ok;
        fillSetOk <- fillSet.ok;
        gridResult <- params == null ? { ok = false, hasValue = false } :
            procgen.generate(algorithm, params);
        gridOk <- gridResult.ok && gridResult.hasValue &&
            gridResult.value.ownership() == "owned" && !gridResult.value.isStale();
        width <- params == null ? 0 : params.getWidth();
        height <- params == null ? 0 : params.getHeight();
        fillKind <- fillIndex < 0 ? "" : procgen.getAlgorithmParamKind(algorithm, fillIndex);
        fillMin <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMinimum(algorithm, fillIndex);
        fillMax <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMaximum(algorithm, fillIndex);
        wfcChoiceCount <- procgen.getAlgorithmParamChoiceCount("wfc.simple", 3);
        wfcFirstChoice <- procgen.getAlgorithmParamChoice("wfc.simple", 3, 0);
        algorithmSchemaResult <- procgen.getAlgorithmSchema(algorithm);
        algorithmSchemaOk <- algorithmSchemaResult.ok && algorithmSchemaResult.hasValue;
        algorithmSchema <- algorithmSchemaOk ? algorithmSchemaResult.value : null;
        genericAlgorithmId <- algorithmSchema == null ? "" : algorithmSchema.getId();
        textureParamsResult <- procgen.newParams();
        textureParamsResultOk <- textureParamsResult.ok;
        textureParams <- textureParamsResult.ok ? textureParamsResult.value : null;
        textureDefaultsResult <- textureParams == null ? { ok = false } :
            procgen.applyTextureRecipeDefaults("tex.rock", textureParams);
        textureDefaultsApplied <- textureDefaultsResult.ok;
        textureSchemaResult <- procgen.getTextureRecipeSchema("tex.rock");
        textureSchemaOk <- textureSchemaResult.ok && textureSchemaResult.hasValue;
        textureSchema <- textureSchemaOk ? textureSchemaResult.value : null;
        textureCategory <- textureSchema == null ? "" : textureSchema.getCategory();
        textureScaleFound <- false;
        for (local i = 0; textureSchema != null && i < textureSchema.getParamCount(); ++i)
            if (textureSchema.getParamKey(i) == "scale") textureScaleFound = true;
        pbrParamsResult <- procgen.newParams();
        pbrParamsResultOk <- pbrParamsResult.ok;
        pbrParams <- pbrParamsResult.ok ? pbrParamsResult.value : null;
        sizeSet <- pbrParams == null ? { ok = false } : pbrParams.setSize(16, 12);
        pbrDefaultsResult <- pbrParams == null ? { ok = false } :
            procgen.applyPbrRecipeDefaults("pbr.rock", pbrParams);
        pbrDefaultsApplied <- pbrDefaultsResult.ok && sizeSet.ok;
        pbrSchemaResult <- procgen.getPbrRecipeSchema("pbr.rock");
        pbrSchemaOk <- pbrSchemaResult.ok && pbrSchemaResult.hasValue;
        pbrSchema <- pbrSchemaOk ? pbrSchemaResult.value : null;
        pbrMetallicFound <- false;
        pbrNormalFound <- false;
        for (local i = 0; pbrSchema != null && i < pbrSchema.getParamCount(); ++i) {
            local key = pbrSchema.getParamKey(i);
            if (key == "metallic") pbrMetallicFound = true;
            if (key == "normalStrength") pbrNormalFound = true;
        }
        pbrSetResult <- pbrParams == null ? { ok = false, hasValue = false } :
            procgen.generatePbrMaterial("pbr.rock", pbrParams);
        pbrResultOk <- pbrSetResult.ok && pbrSetResult.hasValue &&
            pbrSetResult.value.ownership() == "owned" && !pbrSetResult.value.isStale();
        meshParamsResult <- procgen.newParams();
        meshParamsResultOk <- meshParamsResult.ok;
        meshParams <- meshParamsResult.ok ? meshParamsResult.value : null;
        meshDefaultsResult <- meshParams == null ? { ok = false } :
            procgen.applyMeshRecipeDefaults("mesh.fence", meshParams);
        meshDefaultsApplied <- meshDefaultsResult.ok;
        meshSchemaResult <- procgen.getMeshRecipeSchema("mesh.fence");
        meshSchemaOk <- meshSchemaResult.ok && meshSchemaResult.hasValue;
        meshSchema <- meshSchemaOk ? meshSchemaResult.value : null;
        meshSegmentsFound <- false;
        for (local i = 0; meshSchema != null && i < meshSchema.getParamCount(); ++i)
            if (meshSchema.getParamKey(i) == "segments") meshSegmentsFound = true;
        meshBuildResult <- meshParams == null ? { ok = false, hasValue = false } :
            procgen.buildMesh("mesh.fence", meshParams);
        meshBuildOk <- meshBuildResult.ok && meshBuildResult.hasValue &&
            meshBuildResult.value.ownership() == "owned" && !meshBuildResult.value.isStale();
    )"));

    CHECK(vm.find("paramsResultOk").toBool());
    CHECK(vm.find("defaultsApplied").toBool());
    CHECK(vm.find("widthSetOk").toBool());
    CHECK(vm.find("heightSetOk").toBool());
    CHECK(vm.find("seedSetOk").toBool());
    CHECK(vm.find("fillSetOk").toBool());
    CHECK(vm.find("gridOk").toBool());
    CHECK_GT(vm.find("paramCount").toInt(), 3);
    CHECK(vm.find("fieldsComplete").toBool());
    CHECK_EQ(vm.find("fillKind").toString(), std::string("float"));
    CHECK_EQ(vm.find("fillMin").toFloat(), 0.f);
    CHECK_EQ(vm.find("fillMax").toFloat(), 1.f);
    CHECK_EQ(vm.find("width").toInt(), 40);
    CHECK_EQ(vm.find("height").toInt(), 24);
    CHECK_EQ(vm.find("wfcChoiceCount").toInt(), 3);
    CHECK_EQ(vm.find("wfcFirstChoice").toString(), std::string("dungeon"));
    CHECK_EQ(vm.find("genericAlgorithmId").toString(), std::string("cave.cellular"));
    CHECK(vm.find("textureParamsResultOk").toBool());
    CHECK(vm.find("textureDefaultsApplied").toBool());
    CHECK_EQ(vm.find("textureCategory").toString(), std::string("Texture"));
    CHECK(vm.find("textureScaleFound").toBool());
    CHECK(vm.find("pbrParamsResultOk").toBool());
    CHECK(vm.find("pbrDefaultsApplied").toBool());
    CHECK(vm.find("pbrMetallicFound").toBool());
    CHECK(vm.find("pbrNormalFound").toBool());
    CHECK(vm.find("pbrResultOk").toBool());
    CHECK(vm.find("meshParamsResultOk").toBool());
    CHECK(vm.find("meshDefaultsApplied").toBool());
    CHECK(vm.find("meshSegmentsFound").toBool());
    CHECK(vm.find("meshBuildOk").toBool());
}

// procgen_shape_grammar.cpp
using namespace eve::procgen;


// procgen_spatial_data.cpp
using namespace eve::procgen;

namespace {

class FlatProcgenWorldQuery final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY, std::uint64_t) override {
        if (maxY < 4.f || minY > 4.f) return eve::Result<eve::ProcgenSurfaceHit>::success({});
        return eve::Result<eve::ProcgenSurfaceHit>::success({true, 91, x, 4.f, z, 0.f, 1.f, 0.f});
    }
};

struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace


TEST_CASE("procgen.spatialData.squirrelFacadeExposesComposablePrimitives") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local pointsResult = procgen.newPointSet();
        if (pointsResult.ok) {
            local points = pointsResult.value;
            points.add(1.0, 0.0, 1.0);
            points.add(5.0, 0.0, 5.0);
            points.add(9.0, 0.0, 9.0);
            points.setFloatAttribute(0, "priority", 2.0);
            points.setFloatAttribute(1, "priority", 0.5);
            points.setFloatAttribute(2, "priority", 3.0);
            points.setIntAttribute(0, "variant", 7);
            points.setBoolAttribute(0, "enabled", true);
            points.setVectorAttribute(0, "wind", 1.0, 2.0, 3.0);
            local worldResult = procgen.boxVolume(0.0, -1.0, 0.0, 10.0, 1.0, 10.0);
            local holeResult = procgen.sphereVolume(5.0, 0.0, 5.0, 1.5);
            if (worldResult.ok && holeResult.ok) {
                local domainResult = procgen.differenceSpatial(worldResult.value, holeResult.value);
                if (domainResult.ok) {
                    local filteredResult = procgen.filterSpatial(points, domainResult.value, false);
                    local priorityResult = procgen.filterFloatAttribute(points, "priority", 1.0, 4.0, false);
                    local transformedResult = procgen.transformPoints(
                        points, 2.0, 0.0, 3.0, 90.0, 1.0, 1.0, 1.0);
                    local transformed3DResult = procgen.transformPoints3D(
                        points, 0.0, 0.0, 0.0, 90.0, 0.0, 0.0, 1.0, 1.0, 1.0);
                    local unionResult = procgen.unionPoints(points, filteredResult.value);
                    local intersectResult = procgen.intersectPoints(points, filteredResult.value);
                    local pointDifferenceResult = procgen.differencePoints(points, filteredResult.value);
                    local copyResult = procgen.copyPoints(filteredResult.value, points, true);
                    local remapResult = procgen.remapDensity(points, 0.0, 1.0, 0.0, 0.5, true);
                    local mathResult = procgen.mathFloatAttribute(
                        points, "priority", "weightedPriority", "multiply", 2.0, 0.0);
                    local sampledResult = procgen.sampleSpatial(domainResult.value, 5.0, 7, 0.0);
                    local poissonResult = procgen.poissonDisk(10, 10, 2.0, 9, 32);
                    if (points.getAttributeType(0, "variant") == "int" &&
                        points.getAttributeType(0, "enabled") == "bool" &&
                        points.getAttributeType(0, "wind") == "vector" &&
                        points.getIntAttribute(0, "variant", 0) == 7 &&
                        points.getBoolAttribute(0, "enabled", false) &&
                        points.getVectorAttributeZ(0, "wind", 0.0) == 3.0 &&
                        filteredResult.ok && filteredResult.value.getCount() == 2 &&
                        priorityResult.ok && priorityResult.value.getCount() == 2 &&
                        transformedResult.ok && transformedResult.value.getCount() == 3 &&
                        transformed3DResult.ok && transformed3DResult.value.getY(0) == -1.0 &&
                        unionResult.ok && unionResult.value.getCount() == 3 &&
                        intersectResult.ok && intersectResult.value.getCount() == 2 &&
                        pointDifferenceResult.ok && pointDifferenceResult.value.getCount() == 1 &&
                        copyResult.ok && copyResult.value.getCount() == 6 &&
                        remapResult.ok && remapResult.value.getDensity(0) == 0.5 &&
                        mathResult.ok && mathResult.value.getFloatAttribute(
                            0, "weightedPriority", 0.0) == 4.0 &&
                        sampledResult.ok && sampledResult.value.getCount() > 0 &&
                        poissonResult.ok && poissonResult.value.getCount() > 0)
                        result = "ok";
                }
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


// procgen_spline_path.cpp
using namespace eve::procgen;

namespace {

[[maybe_unused]] SplinePoint point(float x, float y, float z) {
    SplinePoint value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

}  // namespace


// procgen_worker_contract.cpp
using namespace eve::procgen;

static_assert(std::is_copy_constructible_v<ProcgenGenerationJob>);
static_assert(!std::is_copy_constructible_v<RuntimeGeneration>);
static_assert(!std::is_move_constructible_v<RuntimeGeneration>);


TEST_CASE("procgen.worker.legacyIssueRejectsWrongThread") {
    RuntimeGeneration runtime(42);
    REQUIRE(runtime.addLevel(10.f, 4.f, 2.f) == 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto worker = std::async(std::launch::async, [&runtime] {
        std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
        auto                                checked = runtime.nextGenerationJob();
        return !request && !checked.ok();
    });
    CHECK(worker.get());
    CHECK(runtime.getGeneratingCount() == 0);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    CHECK(std::move(issued).takeValue().has_value());
}

TEST_CASE("procgen.runtimeGeneration.movesValueJobsAcrossWorkerBoundary") {
    RuntimeGeneration runtime(4201);
    REQUIRE(runtime.isOwnerThread());
    const int addedLevel = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(addedLevel, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    ProcgenGenerationJob job   = *optionalJob;
    const int            level = job.getLevel();
    const int            x     = job.getX();
    const int            z     = job.getZ();

    auto worker    = std::async(std::launch::async, [job] {
        PointSet output;
        output.add(job.getMinX(), 0.f, job.getMinZ());
        return ProcgenGenerationCompletion(job, std::move(output));
    });
    auto committed = runtime.completeGenerationJob(worker.get());
    REQUIRE(committed.ok());
    const auto revision = std::move(committed).takeValue();
    CHECK_EQ(revision, uint64_t(1));
    CHECK(runtime.hasCell(level, x, z));
}

TEST_CASE("procgen.runtimeGeneration.rejectsWorkerThreadSchedulerMutation") {
    RuntimeGeneration runtime(4202);
    const int         addedLevel = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(addedLevel, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    const ProcgenGenerationJob job = *optionalJob;

    auto workerAccepted = std::async(std::launch::async, [&runtime, job] {
        PointSet output;
        output.add(0.f, 0.f, 0.f);
        auto result = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
        return result.ok();
    });
    CHECK(!workerAccepted.get());
    CHECK_EQ(runtime.getGeneratingCount(), 1);

    PointSet output;
    output.add(0.f, 0.f, 0.f);
    auto committed = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
    REQUIRE(committed.ok());
}
