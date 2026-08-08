#include "scene/Scene.h"

#include "scene/TransformSystem.h"

#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>
#include <stdexcept>

namespace eve::scene {

Module_IMPL(Scene, new Scene());

namespace {

SceneHost *findHostByName(const std::string &name) {
    if (name.empty()) return nullptr;
    if (ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && meta->name == name) return meta->entity;
    }
    return nullptr;
}

SceneHost *findHostByOwnerId(uint32_t ownerId) {
    if (ownerId == 0) return nullptr;
    if (ecs::current()->getManager<SceneHost>() == nullptr) return nullptr;
    auto view = ecs::View<SceneHost, SceneHost::Meta>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta] = *it;
        if (meta->entity && meta->ownerId == ownerId) return meta->entity;
    }
    return nullptr;
}

/** Script base: `class X extends eve.SceneComponent { function build() { ... } }`. */
const char *kSceneComponentScript = R"SQ(
eve.SceneComponent <- class {
    hostName = ""
    dirty = true
    forceFull = false
    _scene = null

    constructor(sceneInstance = null) {
        _scene = sceneInstance
        hostName = ""
        dirty = true
        forceFull = false
    }

    function setScene(sceneInstance) { _scene = sceneInstance }

    function mountAs(name) {
        hostName = name
        dirty = true
        forceFull = true
        updateIfDirty()
    }

    function setState() { dirty = true }
    function markDirty() { dirty = true }

    // Override: call this.scene().beginNode / addNode / end ...
    function build() {}

    function scene() {
        if (_scene != null) return _scene
        try {
            if (::scene != null) return ::scene
        } catch (e) {}
        _scene = ::eve.Scene()
        return _scene
    }

    function updateIfDirty() {
        if (!dirty) return false
        local s = scene()
        s.beginBuild()
        build()
        local name = hostName
        if (name == null || name == "") name = "default"
        if (forceFull) {
            s.mountBuildAs(name)
            forceFull = false
        } else {
            s.remountBuildAs(name)
        }
        dirty = false
        return true
    }

    function rebuild(force = false) {
        dirty = true
        forceFull = force
        return updateIfDirty()
    }
}
)SQ";

void injectSceneComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kSceneComponentScript,
                                   static_cast<SQInteger>(std::strlen(kSceneComponentScript)),
                                   "SceneComponent.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

}  // namespace

SceneHost *Scene::ensureSelected(const std::string &preferredName) {
    if (selected_) return selected_;
    if (!preferredName.empty()) {
        selected_ = findHostByName(preferredName);
        if (selected_) return selected_;
        selected_ = SceneHost::createHost(preferredName);
        return selected_;
    }
    selected_ = SceneHost::createHost("default");
    return selected_;
}

SceneHost *Scene::mountAs(const std::string &name, NodeDesc root) {
    SceneHost *h = findHostByName(name);
    if (!h) h = SceneHost::createHost(name);
    h->setTree(std::move(root));
    TransformSystem::updateHost(h);
    selected_ = h;
    return h;
}

SceneHost *Scene::mount(NodeDesc root) { return mountAs("default", std::move(root)); }

SceneHost *Scene::remount(NodeDesc root) {
    SceneHost *h = ensureSelected("default");
    h->setTree(std::move(root));
    TransformSystem::updateHost(h);
    return h;
}

SceneHost *Scene::remountReconcile(NodeDesc root) {
    SceneHost *h = ensureSelected("default");
    h->setTreeReconcile(std::move(root));
    TransformSystem::updateHost(h);
    return h;
}

SceneHost *Scene::remountAs(const std::string &name, NodeDesc root) {
    return mountAs(name, std::move(root));
}

bool Scene::select(const std::string &name) {
    SceneHost *h = findHostByName(name);
    if (!h) return false;
    selected_ = h;
    return true;
}

SceneHost *Scene::findHost(const std::string &name) const { return findHostByName(name); }

SceneHost *Scene::findHostByOwner(uint32_t ownerId) const { return findHostByOwnerId(ownerId); }

void Scene::bindOwner(uint32_t ownerId) {
    SceneHost *h = ensureSelected();
    h->setOwnerId(ownerId);
}

void Scene::setHostVisible(bool visible) {
    if (auto *h = ensureSelected()) h->setVisible(visible);
}

void Scene::setHostLayer(int layer) {
    if (auto *h = ensureSelected()) h->setLayer(layer);
}

void Scene::updateTransforms() {
    if (selected_) TransformSystem::updateHost(selected_);
    else TransformSystem::updateAll();
}

void Scene::updateTransformsAll() { TransformSystem::updateAll(); }

bool Scene::setNodePosition(const std::string &id, float x, float y, float z) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->x = x;
    n->y = y;
    n->z = z;
    n->localDirty = true;
    h->markTransformDirty();
    return true;
}

bool Scene::setNodeRotation(const std::string &id, float yaw, float pitch, float roll) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->yaw = yaw;
    n->pitch = pitch;
    n->roll = roll;
    n->localDirty = true;
    h->markTransformDirty();
    return true;
}

bool Scene::setNodeScale(const std::string &id, float sx, float sy, float sz) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->sx = sx;
    n->sy = sy;
    n->sz = sz;
    n->localDirty = true;
    h->markTransformDirty();
    return true;
}

bool Scene::setNodeVisible(const std::string &id, bool visible) {
    SceneHost *h = selected_;
    if (!h) return false;
    SceneNode *n = h->findById(id);
    if (!n) return false;
    n->visible = visible;
    return true;
}

bool Scene::linkRenderable2D(const std::string &nodeId, graphics::Renderable2D *r) {
    SceneHost *h = selected_;
    if (!h) return false;
    if (!h->linkRenderable2D(nodeId, r)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::linkRenderable3D(const std::string &nodeId, graphics::Renderable3D *r) {
    SceneHost *h = selected_;
    if (!h) return false;
    if (!h->linkRenderable3D(nodeId, r)) return false;
    TransformSystem::updateHost(h);
    return true;
}

bool Scene::unlinkNode(const std::string &nodeId) {
    SceneHost *h = selected_;
    if (!h) return false;
    return h->unlink(nodeId);
}

bool Scene::hasNode(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->hasNode(id) : false;
}

int Scene::getNodeCount() {
    SceneHost *h = selected_;
    return h ? h->getNodeCount() : 0;
}

std::string Scene::getRootId() {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *r = h->getRoot();
    return r ? r->id : std::string{};
}

std::string Scene::getParentId(const std::string &id) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *p = h->getParentById(id);
    return p ? p->id : std::string{};
}

int Scene::getChildCount(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->getChildCountById(id) : 0;
}

std::string Scene::getChildIdAt(const std::string &parentId, int childOrdinal) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *c = h->getChildAtById(parentId, childOrdinal);
    return c ? c->id : std::string{};
}

std::string Scene::findIdByName(const std::string &name) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *n = h->findByName(name);
    return n ? n->id : std::string{};
}

std::string Scene::findIdByPath(const std::string &path) {
    SceneHost *h = selected_;
    if (!h) return {};
    SceneNode *n = h->findByPath(path);
    return n ? n->id : std::string{};
}

std::string Scene::getNodePath(const std::string &id) {
    SceneHost *h = selected_;
    return h ? h->getPathById(id) : std::string{};
}

bool Scene::isAncestor(const std::string &ancestorId, const std::string &nodeId) {
    SceneHost *h = selected_;
    return h ? h->isAncestorOfById(ancestorId, nodeId) : false;
}

bool Scene::isDescendant(const std::string &nodeId, const std::string &ancestorId) {
    SceneHost *h = selected_;
    return h ? h->isDescendantOfById(nodeId, ancestorId) : false;
}

std::vector<std::string> Scene::collectIds() {
    SceneHost *h = selected_;
    return h ? h->collectIds() : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectIdsFrom(const std::string &id) {
    SceneHost *h = selected_;
    if (!h) return {};
    return h->collectIdsFrom(h->findIndexById(id));
}

std::vector<std::string> Scene::collectIdsByName(const std::string &name) {
    SceneHost *h = selected_;
    return h ? h->collectIdsByName(name) : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectIdsVisible(bool visible) {
    SceneHost *h = selected_;
    return h ? h->collectIdsVisible(visible) : std::vector<std::string>{};
}

std::vector<std::string> Scene::collectChildIds(const std::string &parentId) {
    SceneHost *h = selected_;
    return h ? h->getChildIds(parentId) : std::vector<std::string>{};
}

std::vector<std::string> Scene::walkDepthFirstIds() { return collectIds(); }

std::vector<std::string> Scene::walkBreadthFirstIds() {
    SceneHost *h = selected_;
    if (!h) return {};
    std::vector<std::string> out;
    h->walkBreadthFirst([&](SceneHost *, int, SceneNode &n) { out.push_back(n.id); });
    return out;
}

void Scene::beginBuild() {
    openStack_.clear();
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
}

void Scene::pushOpen(NodeDesc d) { openStack_.push_back(std::move(d)); }

NodeDesc &Scene::currentParent() {
    if (openStack_.empty()) throw std::runtime_error("scene: node outside beginNode/beginGroup");
    return openStack_.back();
}

void Scene::beginNode(const std::string &id, const std::string &name) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(node(id, {}, name));
}

void Scene::beginGroup(const std::string &id) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(group({}, id));
}

void Scene::end() {
    if (openStack_.empty()) throw std::runtime_error("scene: end() without begin");
    NodeDesc finished = std::move(openStack_.back());
    openStack_.pop_back();
    if (openStack_.empty()) {
        builtRoot_ = std::move(finished);
        hasBuiltRoot_ = true;
    } else {
        openStack_.back().children.push_back(std::move(finished));
    }
}

void Scene::addNode(const std::string &id, const std::string &name) {
    currentParent().children.push_back(node(id, {}, name));
}

void Scene::setBuildPosition(float x, float y, float z) {
    NodeDesc &d = currentParent();
    d.x = x;
    d.y = y;
    d.z = z;
}

void Scene::setBuildRotation(float yaw, float pitch, float roll) {
    NodeDesc &d = currentParent();
    d.yaw = yaw;
    d.pitch = pitch;
    d.roll = roll;
}

void Scene::setBuildScale(float sx, float sy, float sz) {
    NodeDesc &d = currentParent();
    d.sx = sx;
    d.sy = sy;
    d.sz = sz;
}

void Scene::setBuildSpace(const std::string &space) { currentParent().space = space; }

void Scene::setBuildVisible(bool visible) { currentParent().visible = visible; }

bool Scene::buildComplete() const { return openStack_.empty() && hasBuiltRoot_; }

bool Scene::mountBuild() {
    if (!buildComplete()) return false;
    remount(std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

bool Scene::mountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    mountAs(name, std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

bool Scene::remountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    SceneHost *h = findHost(name);
    if (!h) h = SceneHost::createHost(name);
    h->setTreeReconcile(std::move(builtRoot_));
    TransformSystem::updateHost(h);
    selected_ = h;
    hasBuiltRoot_ = false;
    builtRoot_ = NodeDesc{};
    return true;
}

void Scene::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Scene::create, false);
    expose(cls);
    injectSceneComponentClass(table);
}

void Scene::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Scene::getName);
    cls.addFunc("select", &Scene::select);
    cls.addFunc("bindOwner", &Scene::bindOwner);
    cls.addFunc("setHostVisible", &Scene::setHostVisible);
    cls.addFunc("setHostLayer", &Scene::setHostLayer);
    cls.addFunc("updateTransforms", &Scene::updateTransforms);
    cls.addFunc("updateTransformsAll", &Scene::updateTransformsAll);
    cls.addFunc("setNodePosition", &Scene::setNodePosition);
    cls.addFunc("setNodeRotation", &Scene::setNodeRotation);
    cls.addFunc("setNodeScale", &Scene::setNodeScale);
    cls.addFunc("setNodeVisible", &Scene::setNodeVisible);
    cls.addFunc("linkRenderable2D", &Scene::linkRenderable2D);
    cls.addFunc("linkRenderable3D", &Scene::linkRenderable3D);
    cls.addFunc("unlinkNode", &Scene::unlinkNode);

    cls.addFunc("hasNode", &Scene::hasNode);
    cls.addFunc("getNodeCount", &Scene::getNodeCount);
    cls.addFunc("getRootId", &Scene::getRootId);
    cls.addFunc("getParentId", &Scene::getParentId);
    cls.addFunc("getChildCount", &Scene::getChildCount);
    cls.addFunc("getChildIdAt", &Scene::getChildIdAt);
    cls.addFunc("findIdByName", &Scene::findIdByName);
    cls.addFunc("findIdByPath", &Scene::findIdByPath);
    cls.addFunc("getNodePath", &Scene::getNodePath);
    cls.addFunc("isAncestor", &Scene::isAncestor);
    cls.addFunc("isDescendant", &Scene::isDescendant);
    cls.addFunc("collectIds", &Scene::collectIds);
    cls.addFunc("collectIdsFrom", &Scene::collectIdsFrom);
    cls.addFunc("collectIdsByName", &Scene::collectIdsByName);
    cls.addFunc("collectIdsVisible", &Scene::collectIdsVisible);
    cls.addFunc("collectChildIds", &Scene::collectChildIds);
    cls.addFunc("walkDepthFirstIds", &Scene::walkDepthFirstIds);
    cls.addFunc("walkBreadthFirstIds", &Scene::walkBreadthFirstIds);

    cls.addFunc("beginBuild", &Scene::beginBuild);
    cls.addFunc("beginNode", &Scene::beginNode);
    cls.addFunc("beginGroup", &Scene::beginGroup);
    cls.addFunc("end", &Scene::end);
    cls.addFunc("addNode", &Scene::addNode);
    cls.addFunc("setBuildPosition", &Scene::setBuildPosition);
    cls.addFunc("setBuildRotation", &Scene::setBuildRotation);
    cls.addFunc("setBuildScale", &Scene::setBuildScale);
    cls.addFunc("setBuildSpace", &Scene::setBuildSpace);
    cls.addFunc("setBuildVisible", &Scene::setBuildVisible);
    cls.addFunc("mountBuild", &Scene::mountBuild);
    cls.addFunc("mountBuildAs", &Scene::mountBuildAs);
    cls.addFunc("remountBuildAs", &Scene::remountBuildAs);
}

}  // namespace eve::scene
