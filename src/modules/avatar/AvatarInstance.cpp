#include "avatar/AvatarInstance.h"
#include "avatar/Avatar.h"
#include "animation/Tween.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "model3d/ModelData.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace eve::avatar {
namespace {

std::string trimCopy(const std::string &s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool parseBoolish(const std::string &v, bool &out) {
    std::string t;
    t.reserve(v.size());
    for (char c : v) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (t == "1" || t == "true" || t == "on" || t == "yes") {
        out = true;
        return true;
    }
    if (t == "0" || t == "false" || t == "off" || t == "no") {
        out = false;
        return true;
    }
    return false;
}

}  // namespace

AvatarInstance::AvatarInstance(std::string kind) : kind_(std::move(kind)) {
    if (auto *mod = ModuleManager::getInstance<Avatar>("Avatar"))
        mod->registerInstance(this);
}

AvatarInstance::~AvatarInstance() { release(); }

void AvatarInstance::release() {
    if (released_) return;
    released_ = true;
    tween_ = nullptr;
    if (auto *mod = ModuleManager::getInstance<Avatar>("Avatar"))
        mod->unregisterInstance(this);
    destroyLayers();
    destroyVroid();
    delete live2d_;
    live2d_ = nullptr;
}

void AvatarInstance::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void AvatarInstance::setScale(float sx, float sy) {
    sx_ = sx;
    sy_ = sy;
}

void AvatarInstance::setVisible(bool visible) { visible_ = visible; }

void AvatarInstance::setLayer(int layer) { layer_ = layer; }

void AvatarInstance::ensureParameter(const std::string &name, float value) {
    if (name.empty()) return;
    if (parameters_.find(name) == parameters_.end()) parameterOrder_.push_back(name);
    parameters_[name] = value;
}

void AvatarInstance::setExpression(const std::string &name) {
    expression_ = name;
    if (kind_ == "image") {
        applyExpression(name);
    } else if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setExpression(name);
    } else if (kind_ == "vroid") {
        if (expressionDefs_.count(name)) {
            applyExpression(name);
        } else if (!name.empty()) {
            // Solo morph: zero known morph params then set named weight to 1.
            if (boundMesh_ && boundMesh_->hasMorphData()) {
                for (int i = 0; i < boundMesh_->getMorphCount(); ++i) {
                    const std::string mn = boundMesh_->getMorphName(i);
                    ensureParameter(mn, 0.f);
                }
            }
            ensureParameter(name, 1.f);
            syncMorphWeightsToMesh();
        }
    }
}

void AvatarInstance::setMotion(const std::string &name) {
    motion_ = name;
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setMotion(name);
    }
    if (kind_ == "vroid") setParameter("motion:" + name, 1.f);
}

void AvatarInstance::setParameter(const std::string &name, float value) {
    if (name.empty()) return;
    ensureParameter(name, value);
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->setParameter(name, value);
    } else if (kind_ == "vroid") {
        if (boundMesh_ && boundMesh_->hasMorph(name)) boundMesh_->setMorphWeight(name, value);
    } else if (kind_ == "image") {
        // Parameter name matching a layer drives that layer's alpha (lip-sync etc.).
        if (Layer *L = findLayer(name)) {
            float a = value;
            if (a < 0.f) a = 0.f;
            if (a > 1.f) a = 1.f;
            L->a = a;
            L->visible = a > 0.001f;
        }
    }
}

float AvatarInstance::getParameter(const std::string &name) const {
    if (kind_ == "live2d" && live2d_) return live2d_->getParameter(name);
    auto it = parameters_.find(name);
    return it == parameters_.end() ? 0.f : it->second;
}

bool AvatarInstance::hasParameter(const std::string &name) const {
    if (kind_ == "live2d" && live2d_) {
        // Backend may not expose has; fall through to local cache.
    }
    return parameters_.find(name) != parameters_.end();
}

int AvatarInstance::getParameterCount() const { return int(parameterOrder_.size()); }

std::string AvatarInstance::getParameterName(int index) const {
    if (index < 0 || size_t(index) >= parameterOrder_.size()) return {};
    return parameterOrder_[size_t(index)];
}

void AvatarInstance::update(float dt) {
    applyTweenTracks();
    if (kind_ == "live2d") {
        if (!live2d_) live2d_ = Avatar::createLive2DBackend();
        if (live2d_) live2d_->update(dt);
    }
    (void)dt;
}

void AvatarInstance::sync() {
    if (kind_ == "image")
        syncImageLayers();
    else if (kind_ == "vroid")
        syncVroid();
}

void AvatarInstance::bindTween(animation::Tween *tween) { tween_ = tween; }

void AvatarInstance::unbindTween() { tween_ = nullptr; }

void AvatarInstance::applyTweenTracks() {
    if (!tween_ || !tween_->isActive()) return;
    if (tween_->has("x")) x_ = tween_->get("x");
    if (tween_->has("y")) y_ = tween_->get("y");
    if (tween_->has("sx")) sx_ = tween_->get("sx");
    if (tween_->has("sy")) sy_ = tween_->get("sy");
    if (kind_ == "vroid") {
        if (tween_->has("x3")) x3_ = tween_->get("x3");
        if (tween_->has("y3")) y3_ = tween_->get("y3");
        if (tween_->has("z3")) z3_ = tween_->get("z3");
        if (tween_->has("yaw")) yaw_ = tween_->get("yaw");
    }
    const int n = tween_->getPropertyCount();
    for (int i = 0; i < n; ++i) {
        const std::string name = tween_->getPropertyName(i);
        if (name == "x" || name == "y" || name == "sx" || name == "sy" || name == "x3" ||
            name == "y3" || name == "z3" || name == "yaw")
            continue;
        setParameter(name, tween_->get(name));
    }
}

// ---- image ----

AvatarInstance::Layer *AvatarInstance::findLayer(const std::string &name) {
    for (auto &L : layers_)
        if (L.name == name) return &L;
    return nullptr;
}

const AvatarInstance::Layer *AvatarInstance::findLayer(const std::string &name) const {
    for (const auto &L : layers_)
        if (L.name == name) return &L;
    return nullptr;
}

bool AvatarInstance::addLayer(const std::string &name, graphics::Texture *texture, int zIndex) {
    if (kind_ != "image" || name.empty() || findLayer(name)) return false;
    Layer L;
    L.name = name;
    L.texture = texture;
    L.zIndex = zIndex;
    if (texture) {
        L.w = float(texture->getWidth());
        L.h = float(texture->getHeight());
    }
    layers_.push_back(L);
    return true;
}

bool AvatarInstance::setLayerTexture(const std::string &name, graphics::Texture *texture) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->texture = texture;
    if (texture && L->w <= 0.f && L->h <= 0.f) {
        L->w = float(texture->getWidth());
        L->h = float(texture->getHeight());
    }
    return true;
}

bool AvatarInstance::setLayerVisible(const std::string &name, bool visible) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->visible = visible;
    return true;
}

bool AvatarInstance::setLayerOffset(const std::string &name, float ox, float oy) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->ox = ox;
    L->oy = oy;
    return true;
}

bool AvatarInstance::setLayerColor(const std::string &name, float r, float g, float b, float a) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->r = r;
    L->g = g;
    L->b = b;
    L->a = a;
    return true;
}

bool AvatarInstance::setLayerZ(const std::string &name, int zIndex) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->zIndex = zIndex;
    return true;
}

bool AvatarInstance::setLayerSize(const std::string &name, float w, float h) {
    Layer *L = findLayer(name);
    if (!L) return false;
    L->w = w;
    L->h = h;
    return true;
}

int AvatarInstance::getLayerCount() const { return int(layers_.size()); }

std::string AvatarInstance::getLayerName(int index) const {
    if (index < 0 || size_t(index) >= layers_.size()) return {};
    return layers_[size_t(index)].name;
}

bool AvatarInstance::hasLayer(const std::string &name) const { return findLayer(name) != nullptr; }

bool AvatarInstance::defineExpression(const std::string &name, const std::string &spec) {
    if ((kind_ != "image" && kind_ != "vroid") || name.empty()) return false;
    expressionDefs_[name] = spec;
    return true;
}

bool AvatarInstance::applyExpressionSpec(const std::string &spec) {
    if (spec.empty()) return true;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ';')) {
        part = trimCopy(part);
        if (part.empty()) continue;
        const auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trimCopy(part.substr(0, eq));
        const std::string val = trimCopy(part.substr(eq + 1));
        if (key.empty()) continue;

        if (kind_ == "vroid") {
            bool flag = false;
            if (parseBoolish(val, flag)) {
                setParameter(key, flag ? 1.f : 0.f);
            } else {
                char *end = nullptr;
                const float f = std::strtof(val.c_str(), &end);
                if (end && end != val.c_str())
                    setParameter(key, f);
                else
                    setParameter(key, 1.f);
            }
            continue;
        }

        Layer *L = findLayer(key);
        if (!L) {
            // Unknown layer name still recorded as parameter for tooling.
            setParameter(key, 1.f);
            continue;
        }
        bool flag = false;
        if (parseBoolish(val, flag)) {
            L->visible = flag;
        } else {
            // Non-bool value: show layer and stash as parameter (texture swap by name).
            L->visible = true;
            setParameter(key, 1.f);
            setParameter(key + ".variant", float(std::hash<std::string>{}(val) & 0xffff));
        }
    }
    return true;
}

bool AvatarInstance::applyExpression(const std::string &name) {
    auto it = expressionDefs_.find(name);
    if (it == expressionDefs_.end()) return false;
    expression_ = name;
    const bool ok = applyExpressionSpec(it->second);
    if (kind_ == "vroid") syncMorphWeightsToMesh();
    return ok;
}

void AvatarInstance::syncImageLayers() {
    for (Layer &L : layers_) {
        if (!L.entity) L.entity = graphics::Renderable2D::create();
        auto tf = L.entity->transform();
        auto sp = L.entity->sprite();
        tf->x = x_ + L.ox * sx_;
        tf->y = y_ + L.oy * sy_;
        tf->rot = 0.f;
        tf->sx = sx_;
        tf->sy = sy_;
        float w = L.w;
        float h = L.h;
        if ((w <= 0.f || h <= 0.f) && L.texture) {
            w = float(L.texture->getWidth());
            h = float(L.texture->getHeight());
        }
        if (w <= 0.f) w = 32.f;
        if (h <= 0.f) h = 32.f;
        sp->width = w;
        sp->height = h;
        sp->r = L.r;
        sp->g = L.g;
        sp->b = L.b;
        sp->a = L.a;
        sp->layer = layer_ + L.zIndex;
        sp->visible = visible_ && L.visible;
        sp->texture = L.texture;
        sp->quad = nullptr;
        sp->shader = nullptr;
        sp->canvas = nullptr;
        sp->camera = nullptr;
    }
}

void AvatarInstance::destroyLayers() {
    for (Layer &L : layers_) {
        if (L.entity) {
            ecs::DestroyEntity(L.entity);
            L.entity = nullptr;
        }
    }
    layers_.clear();
}

// ---- live2d ----

bool AvatarInstance::loadLive2DModel(const std::string &path) {
    if (kind_ != "live2d") return false;
    if (!live2d_) live2d_ = Avatar::createLive2DBackend();
    if (!live2d_) return false;
    return live2d_->loadModel(path);
}

std::string AvatarInstance::getLive2DBackendName() const {
    if (live2d_) return live2d_->getName();
    return Avatar::getLive2DBackendName();
}

bool AvatarInstance::hasLive2DBackend() const { return Avatar::live2DBackendFactory() != nullptr; }

// ---- vroid ----

bool AvatarInstance::loadVroidModelPath(const std::string &path) {
    if (kind_ != "vroid") return false;
    vroidPath_ = path;
    return !path.empty();
}

bool AvatarInstance::bindVroidModelData(model3d::ModelData *data) {
    if (kind_ != "vroid") return false;
    vroidData_ = data;
    if (data) loadMorphNamesFromModel(0);
    return data != nullptr;
}

int AvatarInstance::loadMorphNamesFromModel(int meshIndex) {
    if (kind_ != "vroid" || !vroidData_) return 0;
    const int n = vroidData_->getMorphTargetCount(meshIndex);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        const std::string name = vroidData_->getMorphTargetName(meshIndex, i);
        if (name.empty()) continue;
        if (!hasParameter(name)) {
            ensureParameter(name, 0.f);
            ++added;
        }
    }
    return added;
}

void AvatarInstance::setMesh(graphics::Mesh *mesh) {
    if (kind_ != "vroid") return;
    boundMesh_ = mesh;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setMesh(mesh);
    if (mesh && mesh->hasMorphData()) {
        for (int i = 0; i < mesh->getMorphCount(); ++i) {
            const std::string name = mesh->getMorphName(i);
            ensureParameter(name, mesh->getMorphWeight(name));
        }
    }
}

void AvatarInstance::setTexture(graphics::Texture *texture) {
    if (kind_ != "vroid") return;
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setTexture(texture);
}

void AvatarInstance::setPosition3D(float x, float y, float z) {
    x3_ = x;
    y3_ = y;
    z3_ = z;
}

void AvatarInstance::setRotation3D(float yaw, float pitch, float roll) {
    yaw_ = yaw;
    pitch_ = pitch;
    roll_ = roll;
}

void AvatarInstance::setScale3D(float sx, float sy, float sz) {
    sx3_ = sx;
    sy3_ = sy;
    sz3_ = sz;
}

graphics::Mesh *AvatarInstance::getBoundMesh() const { return boundMesh_; }

void AvatarInstance::syncMorphWeightsToMesh() {
    if (!boundMesh_ || !boundMesh_->hasMorphData()) return;
    for (int i = 0; i < boundMesh_->getMorphCount(); ++i) {
        const std::string name = boundMesh_->getMorphName(i);
        boundMesh_->setMorphWeight(name, getParameter(name));
    }
}

bool AvatarInstance::bakeMorphs() {
    syncMorphWeightsToMesh();
    if (!boundMesh_ || !boundMesh_->isMorphDirty()) return false;
    auto *gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx) return false;
    return gfx->bakeMeshMorph(boundMesh_);
}

void AvatarInstance::syncVroid() {
    if (!renderable3d_) renderable3d_ = graphics::Renderable3D::create();
    renderable3d_->setPosition(x3_, y3_, z3_);
    renderable3d_->setRotation(yaw_, pitch_, roll_);
    renderable3d_->setScale(sx3_ * sx_, sy3_ * sy_, sz3_);
    renderable3d_->setVisible(visible_);
    bakeMorphs();
}

void AvatarInstance::destroyVroid() {
    if (renderable3d_) {
        ecs::DestroyEntity(renderable3d_);
        renderable3d_ = nullptr;
    }
    boundMesh_ = nullptr;
    vroidData_ = nullptr;
    vroidPath_.clear();
}

}  // namespace eve::avatar
