#include "scene/editing/SceneTarget.h"

#include "scene/SceneHost.h"

#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
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
    : SceneTargetBase(std::move(id), "runtime-scene-host"), hostHandle_(ecs::handle_of(host)), staging_(!host) {
    if (!host) return;
    const auto            tree = host->tree();
    std::set<std::string> imported;
    while (imported.size() < tree->nodes.size()) {
        const auto before = imported.size();
        for (const scene::SceneNode& node : tree->nodes) {
            if (imported.contains(node.id)) continue;
            if (node.parent >= 0 && !imported.contains(tree->nodes.at(size_t(node.parent)).id)) continue;
            CreateSceneObjectRequest request;
            request.id        = ObjectId(node.id);
            request.parent    = node.parent >= 0 ? ObjectId(tree->nodes[size_t(node.parent)].id) : ObjectId{};
            request.name      = node.name;
            request.transform = {node.x, node.y, node.z, node.pitch, node.yaw, node.roll, node.sx, node.sy, node.sz};
            auto operation    = makeCreate(request);
            if (!operation.ok()) throw std::invalid_argument("Scene host contains invalid editing data");
            auto applied = SceneTargetBase::applyDomainOperation(operation.value());
            if (!applied.ok()) throw std::invalid_argument("Scene host could not be imported");
            imported.insert(node.id);
        }
        if (before == imported.size()) throw std::invalid_argument("Scene host contains invalid hierarchy");
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
    auto* live = host();
    if (!live)
        return liveError<std::vector<SceneComponentLinkSnapshot>>(EditorStatus::Unsupported,
            "editor.scene.live-host-required", "Component links are unavailable on a staging target");
    auto nodeResult = live->findById(object.value());
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
    if (staging_) return SceneTargetBase::applyDomainOperation(operation);
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
    result->hostHandle_ = {};
    result->staging_    = true;
    return result;
}

scene::SceneHost* SceneHostEditorTarget::host() const {
    return dynamic_cast<scene::SceneHost*>(ecs::try_get(hostHandle_));
}

EditorResult<void> SceneHostEditorTarget::synchronizeHost(const SceneTargetBase& desiredTarget) {
    auto* live = host();
    if (!live)
        return liveError<void>(EditorStatus::NotFound, "editor.scene.live-host-required",
                               "Live scene host has been destroyed");
    const auto current = collect(*this);
    const auto desired = collect(desiredTarget);
    const auto tree    = live->tree();
    if (tree->nodes.size() != current.size())
        return liveError<void>(EditorStatus::Conflict, "editor.scene.live-host-diverged",
                               "Live scene structure changed; start a new editing session");
    for (const auto& [id, object] : current) {
        if (!hostMatches(live, object))
            return liveError<void>(EditorStatus::Conflict, "editor.scene.live-host-diverged",
                                   "Live scene changed outside this target: " + id.value());
        const auto* node = live->getNode(live->findIndexById(id.value()));
        if (!desired.contains(id) && (!node->links.empty() || node->objectId != 0))
            return liveError<void>(EditorStatus::Rejected, "editor.scene.live-delete-attached",
                                   "Detach domain links and behaviors before deleting this node");
    }
    // Validate the whole candidate before publishing any host mutation.
    auto valid = desiredTarget.makeRestore(desiredTarget.snapshotValue());
    if (!valid.ok()) return EditorResult<void>::failure(valid.status());
    scene::SceneHost::Tree  candidate;
    std::map<ObjectId, int> indices;
    for (const auto& [id, object] : desired) {
        scene::SceneNode node = runtimeNode(object);
        const int        old  = live->findIndexById(id.value());
        if (old >= 0) {
            node                 = *live->getNode(old);
            const auto transform = runtimeNode(object);
            node.name            = object.name;
            node.x               = transform.x;
            node.y               = transform.y;
            node.z               = transform.z;
            node.pitch           = transform.pitch;
            node.yaw             = transform.yaw;
            node.roll            = transform.roll;
            node.sx              = transform.sx;
            node.sy              = transform.sy;
            node.sz              = transform.sz;
        }
        node.parent = node.firstChild = node.nextSibling = -1;
        node.localDirty = node.subtreeDirty = true;
        indices.emplace(id, static_cast<int>(candidate.nodes.size()));
        candidate.nodes.push_back(std::move(node));
    }
    std::map<int, int> tails;
    for (const auto& [id, object] : desired) {
        const int index               = indices.at(id);
        const int parent              = object.parent.empty() ? -1 : indices.at(object.parent);
        candidate.nodes[index].parent = parent;
        if (tails.contains(parent))
            candidate.nodes[tails.at(parent)].nextSibling = index;
        else if (parent >= 0)
            candidate.nodes[parent].firstChild = index;
        else
            candidate.root = index;
        tails[parent] = index;
    }
    *live->tree() = std::move(candidate);
    return editing::applied<void>();
}

EditorResult<void> SceneHostEditorTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* staged = dynamic_cast<SceneHostEditorTarget*>(candidate.get());
    if (!staged || staged->targetId() != targetId())
        return liveError<void>(EditorStatus::Conflict, "editor.scene.live-candidate-mismatch",
                               "Live scene candidate has an incompatible type");
    EditorResult<void> synchronized = synchronizeHost(*staged);
    if (!synchronized.ok()) return synchronized;
    auto committed = SceneTargetBase::commitDomainState(std::move(candidate));
    if (!committed.ok()) return committed;
    // Observers run only after both published host and transaction baseline agree.
    if (auto* live = host()) live->fireEvent("tree_changed", "");
    return editing::applied<void>();
}

}  // namespace eve::scene_editing
