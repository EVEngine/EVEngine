#include "scene_editing/SceneTarget.h"

#include "scene/SceneHost.h"

#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace eve::scene_editing {
namespace {

template <class T>
EditorResult<T> liveError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

std::map<ObjectId, SceneObjectSnapshot> collect(const SceneTargetBase& target) {
    std::map<ObjectId, SceneObjectSnapshot> result;
    const EditorValue snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return result;
    const auto found = root->find("objects");
    if (found == root->end()) return result;
    const auto* objects = found->second.getIf<EditorValue::Array>();
    if (!objects) return result;
    for (const EditorValue& value : *objects) {
        const auto* object = value.getIf<EditorValue::Object>();
        if (!object) continue;
        const auto* id = object->at("id").getIf<std::string>();
        auto snapshot = target.sceneObject(ObjectId(*id));
        if (snapshot.ok()) result.emplace(snapshot.value().id, snapshot.value());
    }
    return result;
}

scene::SceneNode runtimeNode(const SceneObjectSnapshot& object) {
    scene::SceneNode node;
    node.id = object.id.value();
    node.name = object.name;
    node.x = static_cast<float>(object.transform.x);
    node.y = static_cast<float>(object.transform.y);
    node.z = static_cast<float>(object.transform.z);
    node.pitch = static_cast<float>(object.transform.rotationX);
    node.yaw = static_cast<float>(object.transform.rotationY);
    node.roll = static_cast<float>(object.transform.rotationZ);
    node.sx = static_cast<float>(object.transform.scaleX);
    node.sy = static_cast<float>(object.transform.scaleY);
    node.sz = static_cast<float>(object.transform.scaleZ);
    return node;
}

bool sameTransform(const SceneTransformValue& left, const SceneTransformValue& right) {
    return left == right;
}

bool hostMatches(scene::SceneHost* host, const SceneObjectSnapshot& object) {
    const int index = host->findIndexById(object.id.value());
    scene::SceneNode* node = host->getNode(index);
    if (!node) return false;
    const std::string parent = node->parent >= 0 ? host->getNode(node->parent)->id : std::string{};
    const auto close = [](double left, double right) { return std::abs(left - right) <= 1e-5; };
    return parent == object.parent.value() && node->name == object.name &&
           close(node->x, object.transform.x) && close(node->y, object.transform.y) &&
           close(node->z, object.transform.z) && close(node->pitch, object.transform.rotationX) &&
           close(node->yaw, object.transform.rotationY) && close(node->roll, object.transform.rotationZ) &&
           close(node->sx, object.transform.scaleX) && close(node->sy, object.transform.scaleY) &&
           close(node->sz, object.transform.scaleZ);
}

}  // namespace

SceneHostEditorTarget::SceneHostEditorTarget(std::string id, scene::SceneHost* host)
    : SceneTargetBase(std::move(id), "runtime-scene-host"), host_(host) {
    if (!host_) return;
    const auto tree = host_->tree();
    for (const scene::SceneNode& node : tree->nodes) {
        CreateSceneObjectRequest request;
        request.id = ObjectId(node.id);
        request.parent = node.parent >= 0 ? ObjectId(tree->nodes[size_t(node.parent)].id) : ObjectId{};
        request.name = node.name;
        request.transform = {node.x, node.y, node.z, node.pitch, node.yaw, node.roll,
                             node.sx, node.sy, node.sz};
        auto operation = makeCreate(request);
        if (operation.ok()) {
            auto applied = SceneTargetBase::applyDomainOperation(operation.value());
            applied.ignore();
        }
    }
    clearDirtyRegion();
}

void* SceneHostEditorTarget::queryCapability(const CapabilityId& capability) {
    if (capability == ISceneComponentInspector::editorCapabilityId())
        return static_cast<ISceneComponentInspector*>(this);
    return SceneTargetBase::queryCapability(capability);
}

EditorResult<std::vector<SceneComponentLinkSnapshot>> SceneHostEditorTarget::componentLinks(
    const ObjectId& object) const {
    if (!host_)
        return liveError<std::vector<SceneComponentLinkSnapshot>>(EditorStatus::Unsupported,
            "editor.scene.live-host-required", "Component links are unavailable on a staging target");
    auto nodeResult = host_->findById(object.value());
    if (!nodeResult.ok() || !nodeResult.value())
        return liveError<std::vector<SceneComponentLinkSnapshot>>(EditorStatus::NotFound,
            "editor.scene.component-object-not-found", "Live scene object does not exist: " + object.value());
    std::vector<SceneComponentLinkSnapshot> result;
    for (const scene::SceneLink& link : nodeResult.value()->links) {
        const scene::LinkOps* operations = scene::linkOps(link.kind);
        const bool alive = link.target && (!operations || !operations->alive || operations->alive(link.target));
        result.push_back({scene::linkKindName(link.kind), link.syncMode, alive});
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.kind < b.kind; });
    return eve::editing::applied<std::vector<SceneComponentLinkSnapshot>>(std::move(result));
}

EditorResult<void> SceneHostEditorTarget::applyDomainOperation(const DomainOperation& operation) {
    if (!host_) return SceneTargetBase::applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto* staged = dynamic_cast<SceneHostEditorTarget*>(candidate.get());
    if (!staged)
        return liveError<void>(EditorStatus::Failed, "editor.scene.live-stage-failed",
                               "Could not stage live scene operation");
    EditorResult<void> applied = staged->SceneTargetBase::applyDomainOperation(operation);
    if (!applied.ok()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> SceneHostEditorTarget::cloneDomainState() const {
    auto result = std::make_unique<SceneHostEditorTarget>(*this);
    result->host_ = nullptr;
    return result;
}

EditorResult<void> SceneHostEditorTarget::synchronizeHost(const SceneTargetBase& desiredTarget) {
    if (!host_)
        return liveError<void>(EditorStatus::Rejected, "editor.scene.live-host-required",
                               "Live scene host is unavailable");
    const auto current = collect(*this);
    const auto desired = collect(desiredTarget);
    for (const auto& [id, object] : current) {
        static_cast<void>(object);
        if (!hostMatches(host_, object))
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-host-diverged",
                                   "Live SceneHost changed outside this editor target: " + id.value());
        if (!desired.contains(id) && host_->getChildCountById(id.value()) != 0)
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-delete-not-leaf",
                                   "Live SceneHost object gained children before deletion: " + id.value());
    }
    for (const auto& [id, object] : desired) {
        if (!current.contains(id) && host_->hasNode(id.value()))
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-id-conflict",
                                   "Live SceneHost already contains newly-created id: " + id.value());
        if (object.transform.scaleX == 0.0 || object.transform.scaleY == 0.0 || object.transform.scaleZ == 0.0)
            return liveError<void>(EditorStatus::Rejected, "editor.scene.live-zero-scale",
                                   "Live SceneHost rejects zero scale: " + id.value());
    }

    for (const auto& [id, object] : current) {
        static_cast<void>(object);
        if (!desired.contains(id) && host_->removeLeaf(id.value()) != scene::SceneMutationStatus::Applied)
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-delete-failed",
                                   "Live SceneHost rejected leaf deletion: " + id.value());
    }
    std::set<ObjectId> pending;
    for (const auto& [id, object] : desired)
        if (!current.contains(id)) pending.insert(id);
    while (!pending.empty()) {
        bool progressed = false;
        for (auto iterator = pending.begin(); iterator != pending.end();) {
            const SceneObjectSnapshot& object = desired.at(*iterator);
            if (!object.parent.empty() && !host_->hasNode(object.parent.value())) {
                ++iterator;
                continue;
            }
            if (host_->appendNode(runtimeNode(object), object.parent.value()) != scene::SceneMutationStatus::Applied)
                return liveError<void>(EditorStatus::Conflict, "editor.scene.live-create-failed",
                                       "Live SceneHost rejected object creation: " + object.id.value());
            iterator = pending.erase(iterator);
            progressed = true;
        }
        if (!progressed)
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-parent-order",
                                   "Live SceneHost could not resolve created object parents");
    }
    for (const auto& [id, object] : desired) {
        const auto old = current.find(id);
        if (old != current.end() && old->second.parent != object.parent &&
            !host_->setParentById(id.value(), object.parent.value()))
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-reparent-failed",
                                   "Live SceneHost rejected object reparent: " + id.value());
        if (old == current.end() || old->second.name != object.name)
            if (host_->renameNode(id.value(), object.name) != scene::SceneMutationStatus::Applied)
                return liveError<void>(EditorStatus::Conflict, "editor.scene.live-rename-failed",
                                       "Live SceneHost rejected object rename: " + id.value());
        if (old == current.end() || !sameTransform(old->second.transform, object.transform)) {
            const SceneTransformValue& transform = object.transform;
            if (host_->setLocalTransform(id.value(), static_cast<float>(transform.x),
                                          static_cast<float>(transform.y), static_cast<float>(transform.z),
                                          static_cast<float>(transform.rotationY),
                                          static_cast<float>(transform.rotationX),
                                          static_cast<float>(transform.rotationZ),
                                          static_cast<float>(transform.scaleX),
                                          static_cast<float>(transform.scaleY),
                                          static_cast<float>(transform.scaleZ)) !=
                scene::SceneMutationStatus::Applied)
                return liveError<void>(EditorStatus::Conflict, "editor.scene.live-transform-failed",
                                       "Live SceneHost rejected object transform: " + id.value());
        }
    }
    return eve::editing::applied<void>();
}

EditorResult<void> SceneHostEditorTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* staged = dynamic_cast<SceneHostEditorTarget*>(candidate.get());
    if (!staged)
        return liveError<void>(EditorStatus::Conflict, "editor.scene.live-candidate-mismatch",
                               "Live scene candidate has an incompatible type");
    EditorResult<void> synchronized = synchronizeHost(*staged);
    if (!synchronized.ok()) return synchronized;
    return SceneTargetBase::commitDomainState(std::move(candidate));
}

}  // namespace eve::scene_editing
