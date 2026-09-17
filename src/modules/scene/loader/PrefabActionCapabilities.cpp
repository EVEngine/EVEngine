#include "scene/loader/PrefabActionCapabilities.h"

#include "action/ActionNotifyRegistry.h"
#include "action/ActionPrefabBlock.h"
#include "action/ActionPrefabInstances.h"
#include "action/ActionPreview.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/EntitySpatialResolver.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelRenderer.h"

#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::sceneloader {
namespace {

constexpr double kDegreesToRadians = 0.017453292519943295;
using ActiveKey = std::pair<action::ActionExecutionId, std::string>;

using PrefabLease = action::PrefabInstanceHandle;

template <typename T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

struct WorldTransform {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float yaw = 0.f;
    float pitch = 0.f;
    float roll = 0.f;
    float sx = 1.f;
    float sy = 1.f;
    float sz = 1.f;
};

class RenderablePrefabPool {
public:
    ~RenderablePrefabPool() {
        for (auto& slot : slots_)
            for (const auto entity : slot.entities)
                if (auto* live = ecs::try_get(entity)) ecs::DestroyEntity(live);
    }

    [[nodiscard]] Result<PrefabLease> spawn(const std::string& uri, const WorldTransform& transform,
                                            bool visible = true) {
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            auto& slot = slots_[index];
            if (slot.occupied || slot.retired || slot.uri != uri || !allEntitiesAlive(slot)) continue;
            slot.occupied = true;
            slot.independent = false;
            setSlotVisible(slot, visible);
            apply(slot, transform);
            return Result<PrefabLease>::success(PrefabLease(index, slot.generation));
        }

        auto* graphics = ModuleManager::getInstance<graphics::Graphics>("Graphics");
        auto* models = ModuleManager::getInstance<model3d::Model3D>("Model3D");
        if (!graphics || !models)
            return fail<PrefabLease>(DiagnosticCode::NotFound,
                                     "Prefab spawn requires Graphics and Model3D modules", "uri");
        try {
            auto* model = models->newModelDataFromFile(uri);
            auto renderables = model3d::buildRenderables(*graphics, model);
            if (renderables.empty())
                return fail<PrefabLease>(DiagnosticCode::Failed,
                                         "Prefab model contains no renderable meshes", "uri");
            Slot slot;
            slot.uri = uri;
            slot.occupied = true;
            slot.entities.reserve(renderables.size());
            for (auto* renderable : renderables) slot.entities.push_back(ecs::handle_of(renderable));
            slots_.push_back(std::move(slot));
            const auto index = static_cast<std::uint32_t>(slots_.size() - 1u);
            apply(slots_.back(), transform);
            setSlotVisible(slots_.back(), visible);
            return Result<PrefabLease>::success(PrefabLease(index, slots_.back().generation));
        } catch (const std::exception& error) {
            return fail<PrefabLease>(DiagnosticCode::Failed, error.what(), "uri");
        }
    }

    [[nodiscard]] Result<void> update(PrefabLease lease, const WorldTransform& transform) {
        auto slot = resolve(lease);
        if (!slot) return Result<void>::failure(slot.status());
        apply(*slot.value(), transform);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> recycle(PrefabLease lease) {
        auto slot = resolve(lease);
        if (!slot) return Result<void>::failure(slot.status());
        auto* value = slot.value();
        setSlotVisible(*value, false);
        value->occupied = false;
        value->independent = false;
        const auto next = PrefabLease::nextGeneration(value->generation);
        if (next)
            value->generation = *next;
        else
            value->retired = true;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> setVisible(PrefabLease lease, bool visible) {
        auto slot = resolve(lease);
        if (!slot) return Result<void>::failure(slot.status());
        setSlotVisible(*slot.value(), visible);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> makeIndependent(PrefabLease lease) {
        auto slot = resolve(lease);
        if (!slot) return Result<void>::failure(slot.status());
        slot.value()->independent = true;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] std::vector<action::PrefabInstanceInfo> independentInstances() const {
        std::vector<action::PrefabInstanceInfo> result;
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            const auto& slot = slots_[index];
            if (!slot.occupied || !slot.independent || !allEntitiesAlive(slot)) continue;
            result.push_back({PrefabLease(index, slot.generation), slot.uri});
        }
        return result;
    }

    [[nodiscard]] Result<void> recycleIndependent(PrefabLease lease) {
        auto slot = resolve(lease);
        if (!slot) return Result<void>::failure(slot.status());
        if (!slot.value()->independent)
            return fail<void>(DiagnosticCode::NotFound,
                              "Prefab instance is still owned by an action block", "instance");
        return recycle(lease);
    }

private:
    struct Slot {
        std::string uri;
        std::vector<ecs::EntityHandle> entities;
        std::uint32_t generation = 1;
        bool occupied = false;
        bool independent = false;
        bool retired = false;
    };

    [[nodiscard]] Result<Slot*> resolve(PrefabLease lease) {
        if (!lease.isValid() || lease.index() >= slots_.size())
            return fail<Slot*>(DiagnosticCode::NotFound, "Prefab instance handle is invalid", "instance");
        auto& slot = slots_[lease.index()];
        if (!slot.occupied || slot.generation != lease.generation() || !allEntitiesAlive(slot))
            return fail<Slot*>(DiagnosticCode::NotFound, "Prefab instance handle is missing or stale", "instance");
        return Result<Slot*>::success(&slot);
    }

    static bool allEntitiesAlive(const Slot& slot) {
        if (slot.entities.empty()) return false;
        for (const auto entity : slot.entities)
            if (!ecs::try_get(entity)) return false;
        return true;
    }

    static void setSlotVisible(Slot& slot, bool visible) {
        for (const auto entity : slot.entities)
            if (auto* renderable = dynamic_cast<graphics::Renderable3D*>(ecs::try_get(entity)))
                renderable->setVisible(visible);
    }

    static void apply(Slot& slot, const WorldTransform& transform) {
        for (const auto entity : slot.entities) {
            auto* renderable = dynamic_cast<graphics::Renderable3D*>(ecs::try_get(entity));
            if (!renderable) continue;
            renderable->setPosition(transform.x, transform.y, transform.z);
            renderable->setRotation(transform.yaw, transform.pitch, transform.roll);
            renderable->setScale(transform.sx, transform.sy, transform.sz);
        }
    }

    std::vector<Slot> slots_;
};

WorldTransform previewWorldTransform(const action::ActionSpatialBinding& spatial) {
    return {
        static_cast<float>(spatial.positionOffset.x),
        static_cast<float>(spatial.positionOffset.y),
        static_cast<float>(spatial.positionOffset.z),
        static_cast<float>(spatial.rotationOffsetDegrees.y * kDegreesToRadians),
        static_cast<float>(spatial.rotationOffsetDegrees.x * kDegreesToRadians),
        static_cast<float>(spatial.rotationOffsetDegrees.z * kDegreesToRadians),
        static_cast<float>(spatial.scale.x),
        static_cast<float>(spatial.scale.y),
        static_cast<float>(spatial.scale.z),
    };
}

class PrefabActionPreviewSink final : public action::IActionPreviewSink {
public:
    ~PrefabActionPreviewSink() override {
        discardPrepared();
        recycleAll(current_);
    }

    Result<void> prepare(const action::ActionPreviewFrame& frame) override {
        discardPrepared();
        for (const auto& block : frame.activeBlocks) {
            if (block.type.format() != "gameplay:prefab-spawn") continue;
            auto binding = action::ActionPrefabSpawnBinding::fromPayload(block.payload);
            if (!binding) return Result<void>::failure(binding.status());
            if (binding.value().lifecycle == action::PrefabSpawnLifecycle::CustomDuration &&
                block.localTime >= binding.value().customDuration)
                continue;
            auto lease = pool_.spawn(binding.value().uri, previewWorldTransform(binding.value().spatial), false);
            if (!lease) {
                discardPrepared();
                return Result<void>::failure(lease.status());
            }
            staged_.push_back(lease.value());
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    void present(const action::ActionPreviewFrame&) noexcept override {
        recycleAll(current_);
        current_ = std::move(staged_);
        staged_.clear();
        for (const auto lease : current_) {
            auto visible = pool_.setVisible(lease, true);
            visible.ignore();
        }
    }

    void discardPrepared() noexcept override { recycleAll(staged_); }

private:
    void recycleAll(std::vector<PrefabLease>& leases) noexcept {
        for (const auto lease : leases) {
            auto recycled = pool_.recycle(lease);
            recycled.ignore();
        }
        leases.clear();
    }

    RenderablePrefabPool    pool_;
    std::vector<PrefabLease> current_;
    std::vector<PrefabLease> staged_;
};

class PrefabActionProvider;

class PrefabActionHandler final : public action::IActionNotifyHandler {
public:
    explicit PrefabActionHandler(PrefabActionProvider& provider) : provider_(provider) {}

    Result<void> handle(const action::ActionTimelineEvent& event,
                        const action::ActionNotifyContext& context) override {
        const ActiveKey key{context.executionId, event.itemId.format()};
        if (event.kind == action::ActionTimelineEventKind::StateExit) return exit(key, context);
        if (event.kind != action::ActionTimelineEventKind::StateEnter)
            return fail<void>(DiagnosticCode::InvalidArgument,
                              "Prefab spawn requires state enter or exit", "event.kind");
        if (active_.contains(key))
            return fail<void>(DiagnosticCode::Conflict, "Prefab spawn state is already active", "itemId");
        auto binding = action::ActionPrefabSpawnBinding::fromPayload(event.payload);
        if (!binding) return Result<void>::failure(binding.status());
        auto pose = resolvePose(binding.value().spatial, context);
        if (!pose) return Result<void>::failure(pose.status());
        auto* instances = pool();
        if (!instances)
            return fail<void>(DiagnosticCode::NotFound,
                              "SceneLoader prefab instance service is unavailable", "sceneloader");
        auto lease = instances->spawn(binding.value().uri, worldTransform(binding.value().spatial, pose.value()));
        if (!lease) return Result<void>::failure(lease.status());
        auto deadline = context.time.tryAdd(binding.value().customDuration);
        if (!deadline) {
            auto recycled = instances->recycle(lease.value());
            recycled.ignore();
            return Result<void>::failure(deadline.status());
        }
        active_.emplace(key, Active{lease.value(), std::move(binding).takeValue(),
                                    std::move(pose).takeValue(), deadline.value()});
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    Result<void> update(const action::ActionActiveBlock& block,
                        const action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end())
            return fail<void>(DiagnosticCode::NotFound, "Active prefab state has no instance", "itemId");
        auto& active = found->second;
        if (active.recycled) return Result<void>::success(Status::success(StatusCode::NoOp));
        if (active.binding.lifecycle == action::PrefabSpawnLifecycle::CustomDuration &&
            context.time >= active.deadline) {
            auto* instances = pool();
            if (!instances)
                return fail<void>(DiagnosticCode::NotFound,
                                  "SceneLoader prefab instance service is unavailable", "sceneloader");
            auto recycled = instances->recycle(active.lease);
            if (!recycled) return recycled;
            active.recycled = true;
            return recycled;
        }
        if (active.binding.spatial.mode != action::ActionSpatialAttachmentMode::WorldTransformAtStart) {
            auto pose = resolvePose(active.binding.spatial, context);
            if (!pose) return Result<void>::failure(pose.status());
            if (active.binding.spatial.mode == action::ActionSpatialAttachmentMode::FollowPositionOnly) {
                active.pose.positionX = pose.value().positionX;
                active.pose.positionY = pose.value().positionY;
                active.pose.positionZ = pose.value().positionZ;
            } else {
                active.pose = std::move(pose).takeValue();
            }
        }
        auto* instances = pool();
        if (!instances)
            return fail<void>(DiagnosticCode::NotFound,
                              "SceneLoader prefab instance service is unavailable", "sceneloader");
        return instances->update(active.lease, worldTransform(active.binding.spatial, active.pose));
    }

    Result<void> sample(const action::ActionActiveBlock& block,
                        const action::ActionNotifyContext&) const override {
        auto binding = action::ActionPrefabSpawnBinding::fromPayload(block.payload);
        if (!binding) return Result<void>::failure(binding.status());
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }

    Result<void> advance(const action::ActionNotifyContext& context) override {
        StatusCode outcome = StatusCode::NoOp;
        for (std::size_t index = 0; index < timed_.size();) {
            if (timed_[index].executionId != context.executionId || context.time < timed_[index].deadline) {
                ++index;
                continue;
            }
            auto* instances = pool();
            if (!instances)
                return fail<void>(DiagnosticCode::NotFound,
                                  "SceneLoader prefab instance service is unavailable", "sceneloader");
            auto recycled = instances->recycle(timed_[index].lease);
            if (!recycled) return recycled;
            timed_.erase(timed_.begin() + static_cast<std::ptrdiff_t>(index));
            outcome = StatusCode::Applied;
        }
        return Result<void>::success(Status::success(outcome));
    }

private:
    struct Active {
        PrefabLease lease;
        action::ActionPrefabSpawnBinding binding;
        EntitySpatialPose pose;
        Duration deadline;
        bool recycled = false;
    };
    struct Timed {
        action::ActionExecutionId executionId;
        PrefabLease lease;
        Duration deadline;
    };

    Result<void> exit(const ActiveKey& key, const action::ActionNotifyContext& context) {
        const auto found = active_.find(key);
        if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
        if (found->second.recycled) {
            active_.erase(found);
            return Result<void>::success(Status::success(StatusCode::NoOp));
        }
        if (found->second.binding.lifecycle == action::PrefabSpawnLifecycle::CustomDuration) {
            timed_.push_back({context.executionId, found->second.lease, found->second.deadline});
            active_.erase(found);
            return Result<void>::success(Status::success(StatusCode::Applied));
        }
        auto* instances = pool();
        if (!instances)
            return fail<void>(DiagnosticCode::NotFound,
                              "SceneLoader prefab instance service is unavailable", "sceneloader");
        auto result = found->second.binding.lifecycle == action::PrefabSpawnLifecycle::RecycleOnBlockExit
                          ? instances->recycle(found->second.lease)
                          : instances->makeIndependent(found->second.lease);
        if (!result) return result;
        active_.erase(found);
        return result;
    }

    static Result<EntitySpatialPose> resolvePose(const action::ActionSpatialBinding& spatial,
                                                  const action::ActionNotifyContext& context) {
        std::optional<ecs::EntityHandle> handle;
        OptionalRef<const IAttachmentPointSource> attachments;
        if (spatial.target == action::ActionSpatialTarget::Source) {
            handle = context.source;
            attachments = context.sourceAttachment;
        } else {
            if (spatial.targetIndex >= context.targets.size())
                return fail<EntitySpatialPose>(DiagnosticCode::NotFound,
                                               "Prefab target index is unavailable", "targetIndex");
            handle = context.targets[spatial.targetIndex];
            if (spatial.targetIndex < context.targetAttachments.size())
                attachments = context.targetAttachments[spatial.targetIndex];
        }
        EntitySpatialPose pose;
        if (handle) {
            auto root = resolveEntitySpatialPose(*handle);
            if (!root) return root;
            pose = std::move(root).takeValue();
        }
        if (spatial.bone.empty()) return Result<EntitySpatialPose>::success(std::move(pose));
        if (!attachments)
            return fail<EntitySpatialPose>(DiagnosticCode::Unsupported,
                                           "Prefab bone requires an attachment source", "bone");
        auto point = attachments->get().sampleAttachmentPoint(
            spatial.bone, {static_cast<float>(spatial.positionOffset.x),
                           static_cast<float>(spatial.positionOffset.y),
                           static_cast<float>(spatial.positionOffset.z)});
        if (!point) return Result<EntitySpatialPose>::failure(point.status());
        pose.positionX = point.value().x;
        pose.positionY = point.value().y;
        pose.positionZ = point.value().z;
        return Result<EntitySpatialPose>::success(std::move(pose));
    }

    static WorldTransform worldTransform(const action::ActionSpatialBinding& spatial,
                                         const EntitySpatialPose& pose) {
        const bool bone = !spatial.bone.empty();
        return {
            static_cast<float>(pose.positionX + (bone ? 0.0 : spatial.positionOffset.x)),
            static_cast<float>(pose.positionY + (bone ? 0.0 : spatial.positionOffset.y)),
            static_cast<float>(pose.positionZ + (bone ? 0.0 : spatial.positionOffset.z)),
            static_cast<float>((pose.rotationYDegrees + spatial.rotationOffsetDegrees.y) * kDegreesToRadians),
            static_cast<float>((pose.rotationXDegrees + spatial.rotationOffsetDegrees.x) * kDegreesToRadians),
            static_cast<float>((pose.rotationZDegrees + spatial.rotationOffsetDegrees.z) * kDegreesToRadians),
            static_cast<float>(pose.scaleX * spatial.scale.x),
            static_cast<float>(pose.scaleY * spatial.scale.y),
            static_cast<float>(pose.scaleZ * spatial.scale.z),
        };
    }

    [[nodiscard]] RenderablePrefabPool* pool() const;

    PrefabActionProvider& provider_;
    std::map<ActiveKey, Active> active_;
    std::vector<Timed> timed_;
};

class PrefabActionProvider final : public action::IActionNotifyProvider,
                                   public action::IActionPrefabInstances,
                                   public action::IActionPreviewSinkProvider {
public:
    void start() {
        if (!pool_) pool_ = std::make_unique<RenderablePrefabPool>();
    }

    void shutdown() { pool_.reset(); }

    Result<void> install(action::ActionNotifyRegistry& registry) override {
        if (!pool_)
            return fail<void>(DiagnosticCode::NotFound,
                              "SceneLoader prefab instance service is unavailable", "sceneloader");
        return registry.registerHandler("gameplay:prefab-spawn", std::make_shared<PrefabActionHandler>(*this));
    }

    std::vector<action::PrefabInstanceInfo> independentInstances() const override {
        return pool_ ? pool_->independentInstances() : std::vector<action::PrefabInstanceInfo>{};
    }

    Result<void> recycleIndependent(action::PrefabInstanceHandle handle) override {
        if (!pool_)
            return fail<void>(DiagnosticCode::NotFound,
                              "SceneLoader prefab instance service is unavailable", "sceneloader");
        return pool_->recycleIndependent(handle);
    }

    Result<std::unique_ptr<action::IActionPreviewSink>> createActionPreviewSink() override {
        if (!pool_)
            return fail<std::unique_ptr<action::IActionPreviewSink>>(
                DiagnosticCode::NotFound, "SceneLoader prefab preview service is unavailable", "sceneloader");
        return Result<std::unique_ptr<action::IActionPreviewSink>>::success(
            std::make_unique<PrefabActionPreviewSink>());
    }

    [[nodiscard]] RenderablePrefabPool* pool() const noexcept { return pool_.get(); }

private:
    std::unique_ptr<RenderablePrefabPool> pool_;
};

PrefabActionProvider& provider() {
    static PrefabActionProvider value;
    return value;
}

RenderablePrefabPool* PrefabActionHandler::pool() const { return provider_.pool(); }

}  // namespace

void registerPrefabActionCapabilities() {
    auto& value = provider();
    value.start();
    cap::provide<action::IActionPrefabInstances>(&value);
    cap::addListener<action::IActionPreviewSinkProvider>(&value);
    cap::addListener<action::IActionNotifyProvider>(&value);
}

void shutdownPrefabActionCapabilities() {
    auto& value = provider();
    cap::removeListener<action::IActionNotifyProvider>(&value);
    cap::removeListener<action::IActionPreviewSinkProvider>(&value);
    cap::revoke<action::IActionPrefabInstances>(&value);
    value.shutdown();
}

}  // namespace eve::sceneloader
