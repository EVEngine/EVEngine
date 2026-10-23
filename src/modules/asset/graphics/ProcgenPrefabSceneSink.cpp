#include "asset/graphics/ProcgenPrefabSceneSink.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/graphics/StaticPrefabRenderInstance.h"
#include "common/Assert.h"
#include "graphics/RenderSystem3D.h"

namespace eve::asset_graphics {
namespace {
struct BusyScope {
    bool& busy;
    explicit BusyScope(bool& value) : busy(value) { busy = true; }
    ~BusyScope() { busy = false; }
};
std::array<float, 16> instanceMatrix(const ProcgenInstanceDesc& value) {
    auto matrix = glm::translate(glm::mat4(1.f), glm::vec3(value.x, value.y, value.z));
    matrix      = glm::rotate(matrix, glm::radians(value.yaw), glm::vec3(0.f, 1.f, 0.f));
    matrix      = glm::rotate(matrix, glm::radians(value.pitch), glm::vec3(1.f, 0.f, 0.f));
    matrix      = glm::rotate(matrix, glm::radians(value.roll), glm::vec3(0.f, 0.f, 1.f));
    matrix      = glm::scale(matrix, glm::vec3(value.scaleX, value.scaleY, value.scaleZ));
    std::array<float, 16> output;
    std::copy(glm::value_ptr(matrix), glm::value_ptr(matrix) + 16, output.begin());
    return output;
}
}  // namespace
struct ProcgenPrefabSceneSink::Impl {
    graphics::Graphics& graphics;
    IProcgenSceneSink&  scene;
    struct Batch {
        uint64_t                                                 revision = 0;
        std::vector<ProcgenInstanceDesc>                         source;
        std::vector<std::unique_ptr<StaticPrefabRenderInstance>> renderers;
        std::size_t                                              parts = 0;
    };
    using Batches = std::map<std::string, Batch>;
    struct Version {
        std::shared_ptr<EvpackStaticPrefab>      prefab;
        std::unique_ptr<StaticPrefabResourcePin> pin;
    };
    using VersionKey = std::pair<PersistentId, PersistentId>;
    std::map<VersionKey, Version>   assets;
    Batches                         active;
    std::vector<Batches::node_type> retired;
    bool                            busy = false, closed = false;
    bool                            available() const { return !busy && !closed; }
    Result<void>                    validateRevision(const std::string& id) const {
        const auto found = active.find(id);
        if (found != active.end() && scene.batchRevision(id) != found->second.revision)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                                              "Scene changed outside the prefab renderer projection", id));
        return Result<void>::success();
    }
    static void visibility(StaticPrefabRenderInstance& instance, bool visible) {
        for (std::size_t i = 0; i < instance.rendererCount(); ++i) {
            auto renderer = instance.renderer(i);
            if (renderer.ok()) renderer.value()->setVisible(visible);
        }
    }
    static void visibility(Batch& batch, bool visible) {
        for (auto& instance : batch.renderers) visibility(*instance, visible);
    }
    Result<void> validateCandidates(const Batches& candidates) const {
        for (const auto& [id, batch] : candidates)
            for (const auto& instance : batch.renderers)
                for (std::size_t i = 0; i < instance->rendererCount(); ++i) {
                    auto renderer = instance->renderer(i);
                    if (!renderer.ok()) return Result<void>::failure(renderer.status());
                }
        return Result<void>::success();
    }
    Result<Batches> prepare(const std::vector<ProcgenBatchSnapshot>& snapshots) {
        using Output = Result<Batches>;
        if (snapshots.empty() || snapshots.size() > 64 || retired.size() + snapshots.size() > 64)
            return Output::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                     "prefab batch/retirement budget requires collectRetired"));
        Batches     candidates;
        std::size_t parts = 0, instances = 0;
        for (const auto& [id, batch] : active) {
            parts += batch.parts;
            instances += batch.source.size();
        }
        for (const auto& node : retired) {
            parts += node.mapped().parts;
            instances += node.mapped().source.size();
        }
        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || !snapshot.targetRevision ||
                scene.batchRevision(snapshot.batchId) >= snapshot.targetRevision ||
                candidates.contains(snapshot.batchId))
                return Output::failure(Diagnostic::error(DiagnosticCode::Conflict, "invalid or stale prefab snapshot"));
            auto revision = validateRevision(snapshot.batchId);
            if (!revision.ok()) return Output::failure(revision.status());
            if (instances > 65536 || snapshot.instances.size() > 65536 - instances)
                return Output::failure(
                    Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab instance budget exceeded"));
            instances += snapshot.instances.size();
            auto& batch    = candidates[snapshot.batchId];
            batch.revision = snapshot.targetRevision;
            batch.source   = snapshot.instances;
            std::unordered_set<std::string> ids;
            std::unordered_set<uint64_t>    pointIds;
            for (const auto& value : snapshot.instances) {
                if (value.id.empty() || !value.sourcePointId || !ids.insert(value.id).second ||
                    !pointIds.insert(value.sourcePointId).second)
                    return Output::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "prefab identities must be unique"));
                for (float scalar : {value.x, value.y, value.z, value.yaw, value.pitch, value.roll, value.scaleX,
                                     value.scaleY, value.scaleZ})
                    if (!std::isfinite(scalar))
                        return Output::failure(
                            Diagnostic::error(DiagnosticCode::InvalidArgument, "nonfinite prefab pose"));
                if (!value.assetBinding) continue;
                const auto& binding = *value.assetBinding;
                if (binding.buildId.isNil() || value.asset != binding.asset.format())
                    return Output::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid typed prefab binding"));
                const auto prefab = assets.find({binding.asset.id(), binding.buildId});
                if (prefab == assets.end()) {
                    const auto firstVersion = assets.lower_bound({binding.asset.id(), PersistentId{}});
                    const auto code = firstVersion == assets.end() || firstVersion->first.first != binding.asset.id()
                                          ? DiagnosticCode::NotFound
                                          : DiagnosticCode::Conflict;
                    return Output::failure(
                        Diagnostic::error(code, "prefab asset/build version is not admitted", value.asset));
                }
                const auto count = prefab->second.prefab->drawCount();
                if (parts > 65536 || count > 65536 - parts)
                    return Output::failure(
                        Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab renderer budget exceeded"));
                parts += count;
                batch.parts += count;
                auto preparation =
                    EvpackStaticPrefab::prepareScene(prefab->second.prefab, graphics, value.assetBinding->rootPose);
                if (!preparation.ok()) return Output::failure(preparation.status());
                auto instance =
                    StaticPrefabRenderInstance::create(std::move(preparation).takeValue(), instanceMatrix(value));
                if (!instance.ok()) return Output::failure(instance.status());
                visibility(*instance.value(), false);
                batch.renderers.push_back(std::move(instance).takeValue());
            }
        }
        std::size_t added = 0;
        for (const auto& [id, batch] : candidates)
            if (!active.contains(id)) ++added;
        if (active.size() + added > 64)
            return Output::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "active prefab batch budget exceeded"));
        return Output::success(std::move(candidates));
    }
    void commit(Batches& candidates) {
        while (!candidates.empty()) {
            auto       node = candidates.extract(candidates.begin());
            const auto old  = active.find(node.key());
            if (old != active.end()) {
                visibility(old->second, false);
                retired.push_back(active.extract(old));
            }
            visibility(node.mapped(), true);
            auto inserted = active.insert(std::move(node));
            EV_ASSERT(inserted.inserted, "prepared prefab batch key must be absent at commit");
        }
    }
};
ProcgenPrefabSceneSink::ProcgenPrefabSceneSink(graphics::Graphics& graphics, IProcgenSceneSink& scene)
    : impl_(std::make_unique<Impl>(Impl{graphics, scene})) {}
ProcgenPrefabSceneSink::~ProcgenPrefabSceneSink() {
    auto result = release();
    if (!result.ok()) std::fprintf(stderr, "prefab batch cleanup failed: %s\n", result.status().describe().c_str());
}
Result<void> ProcgenPrefabSceneSink::admit(std::shared_ptr<EvpackStaticPrefab> prefab) {
    if (!impl_->available() || !prefab || !prefab->drawCount() || prefab->sourceBuildId().isNil())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab admission requires live owners"));
    const Impl::VersionKey key{prefab->sourceAsset().id(), prefab->sourceBuildId()};
    if (impl_->assets.contains(key) || impl_->assets.size() >= 4096)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate or oversized prefab catalogue"));
    auto pin = EvpackStaticPrefab::pinResources(prefab);
    if (!pin.ok()) return Result<void>::failure(pin.status());
    impl_->assets.emplace(key, Impl::Version{std::move(prefab), std::move(pin).takeValue()});
    return Result<void>::success();
}
Result<uint64_t> ProcgenPrefabSceneSink::replaceBatchesCoordinated(const std::vector<ProcgenBatchSnapshot>& snapshots,
                                                                   const std::function<Result<void>()>&     owner) {
    if (!impl_->available() || !owner)
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab publication is closed or reentered"));
    BusyScope busy(impl_->busy);
    auto      candidate = impl_->prepare(snapshots);
    if (!candidate.ok()) return Result<uint64_t>::failure(candidate.status());
    impl_->retired.reserve(impl_->retired.size() + snapshots.size());
    return impl_->scene.replaceBatchesCoordinated(snapshots, [&] {
        auto valid = impl_->validateCandidates(candidate.value());
        if (!valid.ok()) return valid;
        auto committed = owner();
        if (!committed.ok()) return committed;
        impl_->commit(candidate.value());
        return Result<void>::success();
    });
}
Result<uint64_t> ProcgenPrefabSceneSink::replaceBatches(const std::vector<ProcgenBatchSnapshot>& snapshots) {
    return replaceBatchesCoordinated(snapshots, [] { return Result<void>::success(); });
}
Result<uint64_t> ProcgenPrefabSceneSink::replaceBatch(const std::string& id, uint64_t revision,
                                                      const std::vector<ProcgenInstanceDesc>& instances) {
    auto result = replaceBatches({{id, revision, instances}});
    if (!result.ok()) return result;
    return Result<uint64_t>::success(revision, result.status());
}
bool ProcgenPrefabSceneSink::applyBatch(const std::string& id, const std::vector<ProcgenInstanceDesc>& instances) {
    if (!impl_->available() || instances.size() > 65536 || batchRevision(id) == std::numeric_limits<uint64_t>::max())
        return false;
    auto                                      target = instances;
    std::unordered_set<uint64_t>              used;
    std::unordered_map<std::string, uint64_t> previous;
    if (const auto found = impl_->active.find(id); found != impl_->active.end())
        for (const auto& value : found->second.source) previous.emplace(value.id, value.sourcePointId);
    for (const auto& value : target)
        if (value.sourcePointId) used.insert(value.sourcePointId);
    uint64_t next = 1;
    for (auto& value : target) {
        if (value.sourcePointId) continue;
        const auto old = previous.find(value.id);
        if (old != previous.end() && used.insert(old->second).second)
            value.sourcePointId = old->second;
        else {
            while (used.contains(next)) ++next;
            value.sourcePointId = next;
            used.insert(next);
        }
    }
    auto result = replaceBatch(id, batchRevision(id) + 1, target);
    return result.ok();
}
Result<uint64_t> ProcgenPrefabSceneSink::applyDelta(const std::string& id, const ProcgenInstanceDelta& delta) {
    if (!impl_->available())
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab delta is reentered or closed"));
    for (std::size_t count : {delta.added.size(), delta.updated.size(), delta.removedPointIds.size(),
                              delta.removed.size(), delta.targetPointOrder.size(), delta.targetOrder.size()})
        if (count > 65536)
            return Result<uint64_t>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                               "prefab delta collection exceeds its work budget"));
    const auto found = impl_->active.find(id);
    if (found == impl_->active.end())
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "prefab delta requires a committed snapshot"));
    auto revision = impl_->validateRevision(id);
    if (!revision.ok()) return Result<uint64_t>::failure(revision.status());
    auto target = buildProcgenInstanceDeltaSnapshot(found->second.source, found->second.revision, delta);
    if (!target.ok()) return Result<uint64_t>::failure(target.status());
    return replaceBatch(id, delta.targetRevision, target.value());
}
Result<uint64_t> ProcgenPrefabSceneSink::removeBatchesCoordinated(const std::vector<std::string>&      ids,
                                                                  const std::function<Result<void>()>& owner) {
    if (!impl_->available() || !owner || ids.empty() || ids.size() > 64 || impl_->retired.size() + ids.size() > 64)
        return Result<uint64_t>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "prefab removal requires live owners and retirement capacity"));
    BusyScope busy(impl_->busy);
    for (const auto& id : ids) {
        auto revision = impl_->validateRevision(id);
        if (!revision.ok()) return Result<uint64_t>::failure(revision.status());
    }
    impl_->retired.reserve(impl_->retired.size() + ids.size());
    return impl_->scene.removeBatchesCoordinated(ids, [&] {
        auto committed = owner();
        if (!committed.ok()) return committed;
        for (const auto& id : ids) {
            const auto old = impl_->active.find(id);
            if (old == impl_->active.end()) continue;
            Impl::visibility(old->second, false);
            impl_->retired.push_back(impl_->active.extract(old));
        }
        return Result<void>::success();
    });
}
Result<uint64_t> ProcgenPrefabSceneSink::removeBatches(const std::vector<std::string>& ids) {
    return removeBatchesCoordinated(ids, [] { return Result<void>::success(); });
}
bool ProcgenPrefabSceneSink::removeBatch(const std::string& id) {
    auto result = removeBatches({id});
    return result.ok();
}
Result<void> ProcgenPrefabSceneSink::collectRetired() {
    if (impl_->busy)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab cleanup cannot reenter publication"));
    for (auto& node : impl_->retired)
        for (auto& instance : node.mapped().renderers) {
            auto result = instance->release();
            if (!result.ok()) return result;
        }
    impl_->retired.clear();
    return Result<void>::success();
}
Result<void> ProcgenPrefabSceneSink::release() {
    if (impl_->closed) return Result<void>::success();
    if (impl_->busy)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab release cannot reenter publication"));
    auto collected = collectRetired();
    if (!collected.ok()) return collected;
    if (!impl_->active.empty()) {
        std::vector<std::string> ids;
        for (const auto& [id, batch] : impl_->active) ids.push_back(id);
        auto removed = removeBatches(ids);
        if (!removed.ok()) return Result<void>::failure(removed.status());
    }
    collected = collectRetired();
    if (!collected.ok()) return collected;
    impl_->assets.clear();
    impl_->closed = true;
    return Result<void>::success();
}
std::size_t ProcgenPrefabSceneSink::rendererCount(const std::string& id) const noexcept {
    const auto found = impl_->active.find(id);
    return found == impl_->active.end() ? 0 : found->second.parts;
}
std::size_t ProcgenPrefabSceneSink::pendingRetirements() const noexcept { return impl_->retired.size(); }
uint64_t ProcgenPrefabSceneSink::batchRevision(const std::string& id) const { return impl_->scene.batchRevision(id); }
int      ProcgenPrefabSceneSink::instanceCount(const std::string& id) const { return impl_->scene.instanceCount(id); }
int ProcgenPrefabSceneSink::lastCreatedCount(const std::string& id) const { return impl_->scene.lastCreatedCount(id); }
int ProcgenPrefabSceneSink::lastReusedCount(const std::string& id) const { return impl_->scene.lastReusedCount(id); }
int ProcgenPrefabSceneSink::lastRemovedCount(const std::string& id) const { return impl_->scene.lastRemovedCount(id); }
}  // namespace eve::asset_graphics
