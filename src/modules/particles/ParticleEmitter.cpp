#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/AnimMath.h"

#include <cmath>
#include <random>
#include <string>

namespace eve::particles {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-6f;

float randRange(std::mt19937 &rng, float a, float b) {
    if (a == b) return a;
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng);
}

int randIndex(std::mt19937 &rng, int n) {
    if (n <= 1) return 0;
    std::uniform_int_distribution<int> dist(0, n - 1);
    return dist(rng);
}

std::string normalizePlane(const std::string &plane) {
    if (plane == "xz" || plane == "yz") return plane;
    return "xy";
}

void projectToPlane(float x, float y, float z, const std::string &plane, float scale, float &ox,
                    float &oy) {
    if (plane == "xz") {
        ox = x * scale;
        oy = z * scale;
    } else if (plane == "yz") {
        ox = y * scale;
        oy = z * scale;
    } else {
        ox = x * scale;
        oy = y * scale;
    }
}

void quatRotateVec(float qx, float qy, float qz, float qw, float vx, float vy, float vz, float &ox,
                   float &oy, float &oz) {
    const float ix = qw * vx + qy * vz - qz * vy;
    const float iy = qw * vy + qz * vx - qx * vz;
    const float iz = qw * vz + qx * vy - qy * vx;
    const float iw = -qx * vx - qy * vy - qz * vz;
    ox = ix * qw + iw * -qx + iy * -qz - iz * -qy;
    oy = iy * qw + iw * -qy + iz * -qx - ix * -qz;
    oz = iz * qw + iw * -qz + ix * -qy - iy * -qx;
}

void sampleEmissionOffset(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float &ox,
                          float &oy) {
    ox = 0.f;
    oy = 0.f;
    if (cfg.areaX <= 0.f && cfg.areaY <= 0.f) return;
    if (cfg.areaType == "ellipse") {
        const float a = randRange(sim.rng, 0.f, kPi * 2.f);
        const float r = std::sqrt(randRange(sim.rng, 0.f, 1.f));
        ox = std::cos(a) * cfg.areaX * r;
        oy = std::sin(a) * cfg.areaY * r;
    } else if (cfg.areaType == "rect") {
        ox = randRange(sim.rng, -cfg.areaX, cfg.areaX);
        oy = randRange(sim.rng, -cfg.areaY, cfg.areaY);
    }
}

void rebuildSkinCandidates(ParticleEmitter::SkinSource &src) {
    src.candidates.clear();
    src.candidatesDirty = false;
    if (!src.skin || src.skin->getVertexCount() <= 0) return;

    const int n = src.skin->getVertexCount();
    const int influences = src.skin->getInfluenceCount();
    if (src.filterBone < 0) {
        src.candidates.reserve(static_cast<size_t>(n));
        for (int v = 0; v < n; ++v) src.candidates.push_back(v);
        return;
    }

    src.candidates.reserve(static_cast<size_t>(n / 4 + 1));
    for (int v = 0; v < n; ++v) {
        float w = 0.f;
        for (int i = 0; i < influences; ++i) {
            if (src.skin->getVertexBone(v, i) == src.filterBone) {
                w += src.skin->getVertexWeight(v, i);
            }
        }
        if (w >= src.minWeight) src.candidates.push_back(v);
    }
}

bool ensureSkinCache(ParticleEmitter::SkinSource &src) {
    if (!src.enabled || !src.skin || !src.pose) return false;
    if (src.candidatesDirty) rebuildSkinCandidates(src);
    if (src.candidates.empty()) return false;
    // Always refresh skinned positions from the live pose (caller must have
    // computeWorld()'d). Cheap relative to VFX; keeps surface following animation.
    return src.skin->updateSkinnedPositions(src.pose);
}

void fillParticleMotion(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, Particle &p) {
    p.lifetime = randRange(sim.rng, cfg.lifeMin, cfg.lifeMax);
    if (p.lifetime <= 0.f) p.lifetime = 1e-4f;
    p.life = p.lifetime;
    p.ax = randRange(sim.rng, cfg.accelXMin, cfg.accelXMax);
    p.ay = randRange(sim.rng, cfg.accelYMin, cfg.accelYMax);
    p.radial = randRange(sim.rng, cfg.radialMin, cfg.radialMax);
    p.tangential = randRange(sim.rng, cfg.tangentialMin, cfg.tangentialMax);
    p.spin = randRange(sim.rng, cfg.spinMin, cfg.spinMax);
    p.rot = 0.f;

    float sizeMul = 1.f;
    if (cfg.sizeVariation > 0.f) {
        const float v = cfg.sizeVariation > 1.f ? 1.f : cfg.sizeVariation;
        sizeMul = 1.f + randRange(sim.rng, -v, v);
        if (sizeMul < 0.01f) sizeMul = 0.01f;
    }
    p.size = sizeMul;

    const float half = cfg.spread * 0.5f;
    const float angle = cfg.direction + randRange(sim.rng, -half, half);
    const float speed = randRange(sim.rng, cfg.speedMin, cfg.speedMax);
    p.vx = std::cos(angle) * speed;
    p.vy = std::sin(angle) * speed;
}

}  // namespace

void spawnParticleAt(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float x, float y) {
    if (sim.alive >= int(sim.particles.size())) return;

    Particle &p = sim.particles[size_t(sim.alive++)];
    float ox = 0.f, oy = 0.f;
    sampleEmissionOffset(cfg, sim, ox, oy);
    p.x = x + ox;
    p.y = y + oy;
    fillParticleMotion(cfg, sim, p);
}

void spawnParticle(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim) {
    // Prefer skin surface when configured on the owning entity.
    if (cfg.entity) {
        auto *skinComp = cfg.entity->skinSource();
        if (skinComp && skinComp->enabled) {
            float sx = cfg.x, sy = cfg.y;
            if (sampleSkinSpawn(*skinComp, sim, sx, sy)) {
                spawnParticleAt(cfg, sim, sx, sy);
                return;
            }
        }
    }
    spawnParticleAt(cfg, sim, cfg.x, cfg.y);
}

bool sampleSkinSpawn(ParticleEmitter::SkinSource &skinSrc, ParticleEmitter::Sim &sim, float &outX,
                     float &outY) {
    if (!ensureSkinCache(skinSrc)) return false;
    const int vi = skinSrc.candidates[static_cast<size_t>(randIndex(sim.rng, int(skinSrc.candidates.size())))];
    const float wx = skinSrc.skin->getSkinnedPositionX(vi);
    const float wy = skinSrc.skin->getSkinnedPositionY(vi);
    const float wz = skinSrc.skin->getSkinnedPositionZ(vi);
    projectToPlane(wx, wy, wz, skinSrc.plane, skinSrc.scale, outX, outY);
    return true;
}

void syncEmitterSources(ParticleEmitter::Config &cfg, ParticleEmitter::Sim & /*sim*/,
                        ParticleEmitter::Attach &attach, ParticleEmitter::SkinSource &skinSrc) {
    if (attach.enabled && attach.pose && attach.boneIndex >= 0 &&
        attach.boneIndex < attach.pose->getBoneCount()) {
        const auto &w = attach.pose->world(attach.boneIndex);
        // Transform local attach offset by bone world matrix.
        float ox = attach.offsetX, oy = attach.offsetY, oz = attach.offsetZ;
        float wx, wy, wz;
        animation::Mat4::fromTRS(w).transformPoint(ox, oy, oz, wx, wy, wz);
        projectToPlane(wx, wy, wz, attach.plane, attach.scale, cfg.x, cfg.y);

        if (attach.followRotation) {
            float fx, fy, fz;
            quatRotateVec(w.qx, w.qy, w.qz, w.qw, 1.f, 0.f, 0.f, fx, fy, fz);
            float ax, ay;
            projectToPlane(fx, fy, fz, attach.plane, 1.f, ax, ay);
            if (ax * ax + ay * ay > kEps) cfg.direction = std::atan2(ay, ax);
        }
    }

    if (skinSrc.enabled && skinSrc.skin && skinSrc.pose) {
        ensureSkinCache(skinSrc);
    }
}

void stepEmitterSim(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float dt) {
    if (sim.paused || dt <= 0.f) return;

    int write = 0;
    for (int i = 0; i < sim.alive; ++i) {
        Particle &p = sim.particles[size_t(i)];
        p.life -= dt;
        if (p.life <= 0.f) continue;

        float ax = p.ax;
        float ay = p.ay;
        const float dx = p.x - cfg.x;
        const float dy = p.y - cfg.y;
        const float len2 = dx * dx + dy * dy;
        if (len2 > kEps) {
            const float inv = 1.f / std::sqrt(len2);
            const float rdx = dx * inv;
            const float rdy = dy * inv;
            ax += rdx * p.radial - rdy * p.tangential;
            ay += rdy * p.radial + rdx * p.tangential;
        }

        p.vx += ax * dt;
        p.vy += ay * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.spin * dt;

        if (write != i) sim.particles[size_t(write)] = p;
        ++write;
    }
    sim.alive = write;

    if (!sim.active) return;

    if (cfg.emitterLife >= 0.f) {
        sim.emitterAge += dt;
        if (sim.emitterAge >= cfg.emitterLife) {
            sim.active = false;
            sim.emitAccum = 0.f;
            return;
        }
    }

    if (cfg.emissionRate <= 0.f) return;
    sim.emitAccum += cfg.emissionRate * dt;
    while (sim.emitAccum >= 1.f) {
        spawnParticle(cfg, sim);
        sim.emitAccum -= 1.f;
        if (sim.alive >= int(sim.particles.size())) {
            sim.emitAccum = 0.f;
            break;
        }
    }
}

ParticleEmitter *ParticleEmitter::createEmitter(int bufferSize) {
    ParticleEmitter *e = ParticleEmitter::create();
    e->config()->entity = e;
    const int n = bufferSize > 0 ? bufferSize : 1;
    e->sim()->particles.resize(size_t(n));
    std::random_device rd;
    e->sim()->rng.seed(rd());
    // Touch Draw / Attach / SkinSource so system views see fully initialized emitters.
    (void)e->draw();
    (void)e->attach();
    (void)e->skinSource();
    return e;
}

void ParticleEmitter::setPosition(float x, float y) {
    config()->x = x;
    config()->y = y;
}

void ParticleEmitter::moveTo(float x, float y) { setPosition(x, y); }

float ParticleEmitter::getX() { return config()->x; }
float ParticleEmitter::getY() { return config()->y; }

void ParticleEmitter::setEmissionRate(float rate) {
    config()->emissionRate = rate < 0.f ? 0.f : rate;
}

float ParticleEmitter::getEmissionRate() { return config()->emissionRate; }

void ParticleEmitter::setParticleLifetime(float minLife, float maxLife) {
    auto c = config();
    c->lifeMin = minLife < 0.f ? 0.f : minLife;
    c->lifeMax = maxLife < c->lifeMin ? c->lifeMin : maxLife;
}

float ParticleEmitter::getParticleLifetimeMin() { return config()->lifeMin; }
float ParticleEmitter::getParticleLifetimeMax() { return config()->lifeMax; }

void ParticleEmitter::setEmitterLifetime(float seconds) { config()->emitterLife = seconds; }
float ParticleEmitter::getEmitterLifetime() { return config()->emitterLife; }

void ParticleEmitter::setDirection(float radians) { config()->direction = radians; }
float ParticleEmitter::getDirection() { return config()->direction; }

void ParticleEmitter::setSpread(float radians) {
    config()->spread = radians < 0.f ? 0.f : radians;
}
float ParticleEmitter::getSpread() { return config()->spread; }

void ParticleEmitter::setSpeed(float minSpeed, float maxSpeed) {
    auto c = config();
    c->speedMin = minSpeed;
    c->speedMax = maxSpeed < minSpeed ? minSpeed : maxSpeed;
}

void ParticleEmitter::setLinearAcceleration(float xmin, float ymin, float xmax, float ymax) {
    auto c = config();
    c->accelXMin = xmin;
    c->accelYMin = ymin;
    c->accelXMax = xmax < xmin ? xmin : xmax;
    c->accelYMax = ymax < ymin ? ymin : ymax;
}

void ParticleEmitter::setRadialAcceleration(float minA, float maxA) {
    auto c = config();
    c->radialMin = minA;
    c->radialMax = maxA < minA ? minA : maxA;
}

void ParticleEmitter::setTangentialAcceleration(float minA, float maxA) {
    auto c = config();
    c->tangentialMin = minA;
    c->tangentialMax = maxA < minA ? minA : maxA;
}

void ParticleEmitter::setEmissionArea(const std::string &type, float x, float y) {
    auto c = config();
    if (type == "ellipse" || type == "rect")
        c->areaType = type;
    else
        c->areaType = "none";
    c->areaX = x < 0.f ? 0.f : x;
    c->areaY = y < 0.f ? 0.f : y;
}

std::string ParticleEmitter::getEmissionAreaType() { return config()->areaType; }
float ParticleEmitter::getEmissionAreaX() { return config()->areaX; }
float ParticleEmitter::getEmissionAreaY() { return config()->areaY; }

void ParticleEmitter::setParticleSize(float width, float height) {
    auto c = config();
    c->particleW = width > 0.f ? width : 1.f;
    c->particleH = height > 0.f ? height : 1.f;
}

float ParticleEmitter::getParticleWidth() { return config()->particleW; }
float ParticleEmitter::getParticleHeight() { return config()->particleH; }

void ParticleEmitter::setSizes(float startScale, float endScale) {
    config()->sizeStart = startScale;
    config()->sizeEnd = endScale;
}

void ParticleEmitter::setSizeVariation(float variation) {
    config()->sizeVariation = variation < 0.f ? 0.f : (variation > 1.f ? 1.f : variation);
}
float ParticleEmitter::getSizeVariation() { return config()->sizeVariation; }

void ParticleEmitter::setSpin(float minSpin, float maxSpin) {
    auto c = config();
    c->spinMin = minSpin;
    c->spinMax = maxSpin < minSpin ? minSpin : maxSpin;
}

void ParticleEmitter::setColorStart(float r, float g, float b, float a) {
    config()->colorStart = Color(r, g, b, a);
}

void ParticleEmitter::setColorEnd(float r, float g, float b, float a) {
    config()->colorEnd = Color(r, g, b, a);
}

void ParticleEmitter::setTexture(graphics::Texture *texture) { draw()->texture = texture; }
graphics::Texture *ParticleEmitter::getTexture() { return draw()->texture; }

void ParticleEmitter::setCanvas(graphics::Canvas *canvas) { draw()->canvas = canvas; }
void ParticleEmitter::setCamera(graphics::Camera2D *camera) { draw()->camera = camera; }

void ParticleEmitter::setLayer(int layer) { draw()->layer = layer; }
int ParticleEmitter::getLayer() { return draw()->layer; }

void ParticleEmitter::setVisible(bool visible) { draw()->visible = visible; }
bool ParticleEmitter::isVisible() { return draw()->visible; }

void ParticleEmitter::start() {
    auto s = sim();
    s->active = true;
    s->paused = false;
    s->emitterAge = 0.f;
}

void ParticleEmitter::stop() {
    auto s = sim();
    s->active = false;
    s->paused = false;
    s->emitAccum = 0.f;
    s->emitterAge = 0.f;
}

void ParticleEmitter::pause() {
    auto s = sim();
    if (s->active) s->paused = true;
}

void ParticleEmitter::reset() {
    auto s = sim();
    s->alive = 0;
    s->emitAccum = 0.f;
    s->emitterAge = 0.f;
    for (auto &p : s->particles) p.life = 0.f;
}

void ParticleEmitter::emit(int count) {
    if (count <= 0) return;
    auto c = config();
    auto s = sim();
    for (int i = 0; i < count; ++i) spawnParticle(*c, *s);
}

bool ParticleEmitter::isActive() {
    auto s = sim();
    return s->active && !s->paused;
}
bool ParticleEmitter::isPaused() { return sim()->paused; }
bool ParticleEmitter::isStopped() { return !sim()->active; }

int ParticleEmitter::getCount() { return sim()->alive; }
int ParticleEmitter::getBufferSize() { return int(sim()->particles.size()); }

void ParticleEmitter::applyPreset(const std::string &name) {
    if (name == "spark") {
        setEmissionRate(80.f);
        setParticleLifetime(0.2f, 0.6f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(kPi * 0.6f);
        setSpeed(60.f, 180.f);
        setLinearAcceleration(-20.f, 40.f, 20.f, 120.f);
        setRadialAcceleration(0.f, 0.f);
        setTangentialAcceleration(0.f, 0.f);
        setEmissionArea("none", 0.f, 0.f);
        setParticleSize(4.f, 4.f);
        setSizes(1.f, 0.2f);
        setSizeVariation(0.3f);
        setSpin(-8.f, 8.f);
        setColorStart(1.f, 0.9f, 0.3f, 1.f);
        setColorEnd(1.f, 0.2f, 0.f, 0.f);
    } else if (name == "smoke") {
        setEmissionRate(25.f);
        setParticleLifetime(1.5f, 3.f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(0.4f);
        setSpeed(10.f, 40.f);
        setLinearAcceleration(-5.f, -30.f, 5.f, -10.f);
        setRadialAcceleration(-5.f, 5.f);
        setTangentialAcceleration(-10.f, 10.f);
        setEmissionArea("ellipse", 12.f, 4.f);
        setParticleSize(16.f, 16.f);
        setSizes(0.5f, 2.f);
        setSizeVariation(0.4f);
        setSpin(-1.f, 1.f);
        setColorStart(0.5f, 0.5f, 0.5f, 0.5f);
        setColorEnd(0.3f, 0.3f, 0.3f, 0.f);
    } else if (name == "fire") {
        setEmissionRate(60.f);
        setParticleLifetime(0.4f, 1.0f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(0.5f);
        setSpeed(20.f, 80.f);
        setLinearAcceleration(-15.f, -80.f, 15.f, -20.f);
        setRadialAcceleration(-20.f, 10.f);
        setTangentialAcceleration(-30.f, 30.f);
        setEmissionArea("ellipse", 20.f, 8.f);
        setParticleSize(10.f, 10.f);
        setSizes(1.2f, 0.3f);
        setSizeVariation(0.25f);
        setSpin(-2.f, 2.f);
        setColorStart(1.f, 0.7f, 0.1f, 1.f);
        setColorEnd(1.f, 0.1f, 0.f, 0.f);
    }
}

bool ParticleEmitter::applyConfig(const std::string &json) {
    return applyConfigText(this, json, nullptr);
}

bool ParticleEmitter::loadConfig(const std::string &path) {
    return loadConfigFile(this, path, nullptr);
}

bool ParticleEmitter::reloadConfig() { return reloadConfigFile(this, nullptr); }

void ParticleEmitter::setAutoReload(bool enable) { resource()->autoReload = enable; }
bool ParticleEmitter::getAutoReload() { return resource()->autoReload; }
std::string ParticleEmitter::getConfigPath() { return resource()->path; }

void ParticleEmitter::attachToBone(animation::AnimPose *pose, int boneIndex) {
    auto *a = attach();
    a->pose = pose;
    a->boneIndex = boneIndex;
    a->enabled = pose != nullptr && boneIndex >= 0;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::attachToBoneByName(animation::AnimPose *pose,
                                         animation::AnimSkeleton *skeleton,
                                         const std::string &boneName) {
    auto *a = attach();
    a->skeleton = skeleton;
    int idx = -1;
    if (skeleton) idx = skeleton->findBone(boneName);
    attachToBone(pose, idx);
}

void ParticleEmitter::setAttachOffset(float x, float y, float z) {
    auto *a = attach();
    a->offsetX = x;
    a->offsetY = y;
    a->offsetZ = z;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::setAttachPlane(const std::string &plane) {
    attach()->plane = normalizePlane(plane);
    if (attach()->enabled) syncAttach();
}

void ParticleEmitter::setAttachScale(float scale) {
    attach()->scale = scale;
    if (attach()->enabled) syncAttach();
}

void ParticleEmitter::setFollowBoneRotation(bool enable) {
    attach()->followRotation = enable;
    if (attach()->enabled) syncAttach();
}

void ParticleEmitter::detach() {
    auto *a = attach();
    a->enabled = false;
    a->pose = nullptr;
    a->boneIndex = -1;
}

bool ParticleEmitter::isAttached() { return attach()->enabled; }
int ParticleEmitter::getAttachBone() { return attach()->boneIndex; }

void ParticleEmitter::syncAttach() {
    syncEmitterSources(*config(), *sim(), *attach(), *skinSource());
}

void ParticleEmitter::setSkinSource(animation::AnimSkin *skin, animation::AnimPose *pose) {
    auto *s = skinSource();
    s->skin = skin;
    s->pose = pose;
    s->enabled = skin != nullptr && pose != nullptr;
    s->candidatesDirty = true;
    s->lastSkinnedFrame = -1;
}

void ParticleEmitter::setSkinBoneFilter(int skeletonBoneIndex, float minWeight) {
    auto *s = skinSource();
    s->filterBone = skeletonBoneIndex;
    s->minWeight = minWeight < 0.f ? 0.f : minWeight;
    s->candidatesDirty = true;
}

void ParticleEmitter::setSkinBoneFilterByName(animation::AnimSkeleton *skeleton,
                                              const std::string &boneName, float minWeight) {
    auto *s = skinSource();
    s->skeleton = skeleton;
    int idx = -1;
    if (skeleton) idx = skeleton->findBone(boneName);
    setSkinBoneFilter(idx, minWeight);
}

void ParticleEmitter::setSkinPlane(const std::string &plane) {
    skinSource()->plane = normalizePlane(plane);
}

void ParticleEmitter::setSkinScale(float scale) { skinSource()->scale = scale; }

void ParticleEmitter::clearSkinSource() {
    auto *s = skinSource();
    s->enabled = false;
    s->skin = nullptr;
    s->pose = nullptr;
    s->filterBone = -1;
    s->candidates.clear();
    s->candidatesDirty = true;
}

bool ParticleEmitter::hasSkinSource() { return skinSource()->enabled; }

void ParticleEmitter::emitFromSkin(int count) {
    if (count <= 0) return;
    auto *s = skinSource();
    if (!s->enabled) return;
    auto c = config();
    auto simc = sim();
    for (int i = 0; i < count; ++i) {
        float sx = c->x, sy = c->y;
        if (!sampleSkinSpawn(*s, *simc, sx, sy)) break;
        spawnParticleAt(*c, *simc, sx, sy);
    }
}

}  // namespace eve::particles
