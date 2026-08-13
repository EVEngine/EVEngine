#pragma once

#include "common/ECS.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Camera2D;
class Canvas;
}

namespace eve::animation {
class AnimPose;
class AnimSkeleton;
class AnimSkin;
class SpineSkeleton;
}

namespace eve::ik {
class Skeleton2D;
class Skeleton3D;
}

namespace eve::particles {

/** Single live particle (CPU simulation). */
struct Particle {
    float x = 0.f;
    float y = 0.f;
    float vx = 0.f;
    float vy = 0.f;
    float ax = 0.f;
    float ay = 0.f;
    float radial = 0.f;
    float tangential = 0.f;
    float life = 0.f;
    float lifetime = 1.f;
    float size = 1.f;
    float rot = 0.f;
    float spin = 0.f;
};

/**
 * ECS emitter entity. Script configures components; ParticleSimSystem /
 * ParticleRenderSystem drive per-frame update & draw.
 */
class ParticleEmitter : public ecs::Entity {
public:
    ENTITY(ParticleEmitter, ecs::Entity)

    void release() override {}

    struct Config {
        float x = 0.f;
        float y = 0.f;
        float emissionRate = 0.f;
        float lifeMin = 1.f;
        float lifeMax = 1.f;
        float emitterLife = -1.f;  // -1 = forever
        float direction = 0.f;
        float spread = 0.f;
        float speedMin = 0.f;
        float speedMax = 0.f;
        float accelXMin = 0.f, accelYMin = 0.f;
        float accelXMax = 0.f, accelYMax = 0.f;
        float radialMin = 0.f, radialMax = 0.f;
        float tangentialMin = 0.f, tangentialMax = 0.f;
        float particleW = 8.f;
        float particleH = 8.f;
        float sizeStart = 1.f;
        float sizeEnd = 1.f;
        float sizeVariation = 0.f;  // 0..1
        float spinMin = 0.f;
        float spinMax = 0.f;
        /** "none" | "ellipse" | "rect" (≤15). */
        std::string areaType = "none";
        float areaX = 0.f;
        float areaY = 0.f;
        Color colorStart{1.f, 1.f, 1.f, 1.f};
        Color colorEnd{1.f, 1.f, 1.f, 0.f};
        ParticleEmitter *entity = nullptr;
    };

    struct Sim {
        std::vector<Particle> particles;
        int alive = 0;
        float emitAccum = 0.f;
        float emitterAge = 0.f;
        bool active = false;
        bool paused = false;
        std::mt19937 rng;
    };

    struct Draw {
        graphics::Texture *texture = nullptr;
        graphics::Canvas *canvas = nullptr;     // nullptr → screen
        graphics::Camera2D *camera = nullptr;   // nullptr → screen space (no camera)
        int layer = 0;
        bool visible = true;
    };

    /** Bound config file for hot reload (empty path = unbound). */
    struct Resource {
        std::string path;
        std::string texturePath;
        int64_t modtime = -1;
        bool autoReload = true;
    };

    /**
     * Optional bone attachment. When enabled, syncAttach() writes Config.x/y
     * (and optionally direction) from a live skeleton each frame.
     * Particles remain 2D; 3D bone XYZ is projected via plane + scale.
     *
     * Supported sources (mutually exclusive):
     * - AnimPose + bone index (3D skeletal animation)
     * - SpineSkeleton + bone index (2D Spine)
     * - IK Skeleton2D / Skeleton3D + bone id (FABRIK chains)
     */
    struct Attach {
        enum class Kind { None, AnimPose, Spine, Ik2D, Ik3D };

        Kind kind = Kind::None;
        animation::AnimPose *pose = nullptr;
        animation::AnimSkeleton *skeleton = nullptr;  // optional (name lookup)
        animation::SpineSkeleton *spine = nullptr;
        eve::ik::Skeleton2D *ik2d = nullptr;
        eve::ik::Skeleton3D *ik3d = nullptr;
        int boneIndex = -1;
        float offsetX = 0.f;
        float offsetY = 0.f;
        float offsetZ = 0.f;
        /** "xy" | "xz" | "yz" — axes mapped to particle plane (3D sources). */
        std::string plane = "xy";
        float scale = 1.f;
        bool followRotation = false;
        bool enabled = false;
    };

    /**
     * Optional skinned-mesh surface source. When enabled, newly spawned
     * particles sample random (optionally bone-filtered) skinned vertices.
     */
    struct SkinSource {
        animation::AnimSkin *skin = nullptr;
        animation::AnimPose *pose = nullptr;
        animation::AnimSkeleton *skeleton = nullptr;  // optional (name filter)
        int filterBone = -1;                          // skeleton bone index, -1 = all
        float minWeight = 0.f;
        /** "xy" | "xz" | "yz" */
        std::string plane = "xy";
        float scale = 1.f;
        bool enabled = false;
        /** Vertex indices eligible for sampling (rebuilt when filter changes). */
        std::vector<int> candidates;
        bool candidatesDirty = true;
        int lastSkinnedFrame = -1;
    };

    COMPONENT(Config, config)
    COMPONENT(Sim, sim)
    COMPONENT(Draw, draw)
    COMPONENT(Resource, resource)
    COMPONENT(Attach, attach)
    COMPONENT(SkinSource, skinSource)

    static ParticleEmitter *createEmitter(int bufferSize = 1000);

    void setPosition(float x, float y);
    void moveTo(float x, float y);
    float getX();
    float getY();

    void setEmissionRate(float rate);
    float getEmissionRate();

    void setParticleLifetime(float minLife, float maxLife);
    float getParticleLifetimeMin();
    float getParticleLifetimeMax();

    void setEmitterLifetime(float seconds);
    float getEmitterLifetime();

    void setDirection(float radians);
    float getDirection();

    void setSpread(float radians);
    float getSpread();

    void setSpeed(float minSpeed, float maxSpeed);
    void setLinearAcceleration(float xmin, float ymin, float xmax, float ymax);
    void setRadialAcceleration(float minA, float maxA);
    void setTangentialAcceleration(float minA, float maxA);

    void setEmissionArea(const std::string &type, float x, float y);
    std::string getEmissionAreaType();
    float getEmissionAreaX();
    float getEmissionAreaY();

    void setParticleSize(float width, float height);
    float getParticleWidth();
    float getParticleHeight();

    void setSizes(float startScale, float endScale);
    void setSizeVariation(float variation);
    float getSizeVariation();

    void setSpin(float minSpin, float maxSpin);

    void setColorStart(float r, float g, float b, float a = 1.f);
    void setColorEnd(float r, float g, float b, float a = 1.f);

    void setTexture(graphics::Texture *texture);
    graphics::Texture *getTexture();

    void setCanvas(graphics::Canvas *canvas);
    void setCamera(graphics::Camera2D *camera);

    void setLayer(int layer);
    int getLayer();

    void setVisible(bool visible);
    bool isVisible();

    void start();
    void stop();
    void pause();
    void reset();
    void emit(int count);

    bool isActive();
    bool isPaused();
    bool isStopped();

    int getCount();
    int getBufferSize();

    /** Named preset: "spark" / "smoke" / "fire". Unknown → no-op. */
    void applyPreset(const std::string &name);

    bool applyConfig(const std::string &json);
    bool loadConfig(const std::string &path);
    bool reloadConfig();
    void setAutoReload(bool enable);
    bool getAutoReload();
    std::string getConfigPath();

    // --- Bone attachment (3D AnimPose / 2D Spine / IK 2D·3D) ---
    void attachToBone(animation::AnimPose *pose, int boneIndex);
    void attachToBoneByName(animation::AnimPose *pose, animation::AnimSkeleton *skeleton,
                            const std::string &boneName);
    void attachToSpineBone(animation::SpineSkeleton *spine, int boneIndex);
    void attachToSpineBoneByName(animation::SpineSkeleton *spine, const std::string &boneName);
    void attachToSkeleton2D(eve::ik::Skeleton2D *skeleton, int boneId);
    void attachToSkeleton3D(eve::ik::Skeleton3D *skeleton, int boneId);
    void setAttachOffset(float x, float y, float z);
    void setAttachPlane(const std::string &plane);
    void setAttachScale(float scale);
    void setFollowBoneRotation(bool enable);
    void detach();
    bool isAttached();
    int getAttachBone();
    /** "none" | "anim" | "spine" | "ik2d" | "ik3d" */
    std::string getAttachKind();
    /** Sync Config.x/y (and direction) from the attached bone. Also called by ParticleSimSystem. */
    void syncAttach();

    // --- Skinned mesh surface emission ---
    void setSkinSource(animation::AnimSkin *skin, animation::AnimPose *pose);
    void setSkinBoneFilter(int skeletonBoneIndex, float minWeight = 0.f);
    void setSkinBoneFilterByName(animation::AnimSkeleton *skeleton, const std::string &boneName,
                                 float minWeight = 0.f);
    void setSkinPlane(const std::string &plane);
    void setSkinScale(float scale);
    void clearSkinSource();
    bool hasSkinSource();
    /** Burst-emit `count` particles from the current skinned surface. */
    void emitFromSkin(int count);
};

void spawnParticle(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim);
void spawnParticleAt(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float x, float y);
void stepEmitterSim(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float dt);
/** Sync bone attach + refresh skin cache; call before stepEmitterSim when using Attach/SkinSource. */
void syncEmitterSources(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim,
                        ParticleEmitter::Attach &attach, ParticleEmitter::SkinSource &skinSrc);
bool sampleSkinSpawn(ParticleEmitter::SkinSource &skinSrc, ParticleEmitter::Sim &sim, float &outX,
                     float &outY);

}  // namespace eve::particles
