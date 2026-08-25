#pragma once

#include "common/ECS.h"
#include "common/RenderTypes.h"
#include "particles/ParticleCurve.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Camera2D;
class Canvas;
class Light2D;
class Shader;
class Texture;
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

using eve::Color;

/** @brief Single live particle (CPU simulation). */
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
    /** @brief Flipbook frame progress (float frame index; grid resolved at render). */
    float frame = 0.f;
    /** @brief Random noise phase per particle (turbulence offset). */
    float noisePhase = 0.f;
};

/**
 * @brief ECS emitter entity. Script configures components; ParticleSimSystem /
 * ParticleRenderSystem drive per-frame update & draw.
 */
class ParticleEmitter : public ecs::Entity {
public:
    ENTITY(ParticleEmitter, ecs::Entity)

    void release() override;

    struct Config {
        /** @brief Timed burst emission (fired once while the emitter is active). */
        struct Burst {
            float time = 0.f;
            int count = 0;
            bool emitted = false;
        };

        float x = 0.f;
        float y = 0.f;
        float emissionRate = 0.f;
        /** @brief Distance-based emission in particles per world unit (0 = disabled). */
        float emissionRateOverDistance = 0.f;
        float lifeMin = 1.f;
        float lifeMax = 1.f;
        float emitterLife = -1.f;  // -1 = forever
        /** @brief Restart the emitter timeline after emitterLife instead of stopping. */
        bool looping = false;
        /** @brief Simulation time multiplier. 0 freezes playback without changing pause state. */
        float playbackSpeed = 1.f;
        /** @brief Fixed simulation step in seconds (0 = variable step). */
        float fixedTimeStep = 0.f;
        /** @brief Spiral-of-death guard for fixed stepping. */
        int maxSubSteps = 8;
        /** @brief Stable seed used when autoRandomSeed is false. */
        int randomSeed = 0;
        /** @brief Generate a new seed on each start. */
        bool autoRandomSeed = true;
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
        /** @brief Gravity applied every step (world units/s²). */
        float gravityX = 0.f;
        float gravityY = 0.f;
        /** @brief Per-second velocity damping fraction in [0,1]. */
        float damping = 0.f;
        /** @brief Max speed; 0 = unlimited. Applied after forces each step. */
        float limitVelocity = 0.f;
        /** @brief Optional speed multiplier curve over lifetime. */
        ParticleCurve velocityCurve;
        /** @brief Fraction [0,1] of the emitter's current velocity added to new particles. */
        float inheritVelocity = 0.f;
        /** @brief "world" (default) or "local" (particles track the emitter). */
        std::string simSpace = "world";
        /** @brief Turbulence: random per-particle acceleration scaled by strength. */
        float noiseStrength = 0.f;
        float noiseFrequency = 1.f;
        float noiseSpeed = 1.f;
        /** @brief "none" | "kill" | "bounce" | "stop" on collision. */
        std::string collisionMode = "none";
        float collisionRadius = 0.f;          // 0 = particle size/2
        float collisionRestitution = 0.6f;
        float collisionLifetimeLoss = 0.f;    // fraction of life removed per hit
        bool collisionBoundsEnabled = false;
        float boundsMinX = 0.f;
        float boundsMinY = 0.f;
        float boundsMaxX = 0.f;
        float boundsMaxY = 0.f;
        /** @brief Query the engine-level world collision resolver each step. */
        bool worldCollision = false;
        /** @brief "billboard" | "axis" | "stretched" | "ribbon". */
        std::string renderMode = "billboard";
        float stretchFactor = 1.f;
        /** @brief Fixed screen-space angle in degrees used by axis mode. */
        float renderAxisDegrees = 0.f;
        /** @brief "none" | "oldest" | "youngest" | "distance" transparent ordering. */
        std::string sortMode = "none";
        /** @brief Particle shading policy: "unlit" or "lit". */
        std::string materialMode = "unlit";
        /** @brief Ribbon width multiplier and minimum accepted segment length. */
        float ribbonWidth            = 1.f;
        float ribbonMinSegmentLength = 1.f;
        /** @brief Soft-particle parameters in normalized linear scene depth. */
        bool  softParticles     = false;
        float softParticleDepth = 0.5f;
        float softFadeDistance  = 0.05f;
        /** @brief Buffer-full strategy: "drop" (default) | "pause" | "warn". */
        std::string overflowMode = "drop";
        /** @brief Cap per-step delta time (0 = unlimited). */
        float maxDeltaTime = 0.f;
        /** @brief Prewarm seconds simulated in start() so the effect is pre-filled. */
        float prewarmSeconds = 0.f;
        /** @brief Opt-in GPU-accelerated simulation (falls back to CPU when unavailable). */
        bool gpuSimulation = false;
        /** @brief Higher-priority emitters receive simulation and spawn budget first. */
        int priority = 0;
        /** @brief Lowest global quality tier [0,3] on which this emitter runs. */
        int minimumQuality = 0;
        /** @brief "automatic" | "pause" | "always" offscreen simulation policy. */
        std::string cullingMode = "automatic";
        /** @brief Optional camera distance cutoff in world units; zero disables it. */
        float cullDistance = 0.f;
        /** @brief Per-frame automatic spawn cap; zero is unlimited. */
        int maxSpawnPerFrame = 0;
        std::vector<Burst> bursts;
        /** @brief Radial attract/repel force fields (strength > 0 attract, < 0 repel). */
        struct ForceField {
            float x = 0.f;
            float y = 0.f;
            float radius = 0.f;
            float strength = 0.f;
            float falloff = 1.f;  // exponent; 1 = linear
        };
        std::vector<ForceField> forceFields;
        /** @brief Emitter-level 2D light emission (pooled Light2D entities). */
        struct LightCfg {
            bool enabled = false;
            int max = 4;              // capped at 8 per emitter (engine limit per canvas)
            float radius = 120.f;
            float intensity = 1.f;
            float r = 1.f;
            float g = 1.f;
            float b = 1.f;
        } lights;
        /** @brief Script-linked sub-emitters (birth / death / collision triggers). */
        struct SubEmitter {
            ParticleEmitter *target = nullptr;
            std::string trigger = "birth";  // "birth" | "death" | "collision"
            float inheritVelocity = 0.f;
        };
        std::vector<SubEmitter> subEmitters;
        /** @brief Initial rotation in degrees (random between min/max; radians at sim time). */
        float startRotMin = 0.f;
        float startRotMax = 0.f;
        /** @brief Flipbook grid. 1x1 = static full texture. frameRate = frames/sec (0 = static). */
        int hframes = 1;
        int vframes = 1;
        float frameRate = 0.f;
        /** @brief 0..1 fraction: randomize the starting frame up to this fraction of the sheet. */
        float frameRandomStart = 0.f;
        /** @brief Optional multi-stop gradient; overrides colorStart/colorEnd when non-empty. */
        ParticleGradient colorGradient;
        /** @brief Optional size scale curve over lifetime; overrides sizeStart/sizeEnd. */
        ParticleCurve sizeCurve;
        /** @brief Optional extra rotation (degrees) over lifetime; added on top of spin. */
        ParticleCurve rotationCurve;
        /** @brief "none" | "ellipse" | "rect" (≤15). */
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
        float distanceEmitAccum = 0.f;
        float fixedTimeAccum = 0.f;
        float emitterAge = 0.f;
        bool active = false;
        bool paused = false;
        bool hasLastPos = false;
        float lastX = 0.f;
        float lastY = 0.f;
        bool overflowWarned = false;
        int activeSeed = 0;
        /** @brief Temporary automatic-spawn quota set by ParticleSimSystem (-1 = unlimited). */
        int spawnQuota = -1;
        int spawnedThisFrame = 0;
        int droppedSpawnsThisFrame = 0;
        std::mt19937 rng;
    };

    struct Draw {
        graphics::Texture *texture = nullptr;
        /** @brief Optional tangent-space normal map used by the lit 2D particle path. */
        graphics::Texture*  normalTexture = nullptr;
        graphics::Canvas *canvas = nullptr;     // nullptr → screen
        graphics::Camera2D *camera = nullptr;   // nullptr → screen space (no camera)
        BlendMode blend = BlendMode::Alpha;
        graphics::Shader *shader = nullptr;     // custom fragment pipeline (textured quads only)
        int layer = 0;
        bool visible = true;
    };

    /** @brief Bound config file for hot reload (empty path = unbound). */
    struct Resource {
        std::string path;
        std::string texturePath;
        std::string normalTexturePath;
        int64_t modtime = -1;
        bool autoReload = true;
    };

    /**
     * @brief Optional bone attachment. When enabled, syncAttach() writes Config.x/y
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
        /** @brief "xy" | "xz" | "yz" — axes mapped to particle plane (3D sources). */
        std::string plane = "xy";
        float scale = 1.f;
        bool followRotation = false;
        bool enabled = false;
    };

    /**
     * @brief Optional skinned-mesh surface source. When enabled, newly spawned
     * particles sample random (optionally bone-filtered) skinned vertices.
     */
    struct SkinSource {
        animation::AnimSkin *skin = nullptr;
        animation::AnimPose *pose = nullptr;
        animation::AnimSkeleton *skeleton = nullptr;  // optional (name filter)
        int filterBone = -1;                          // skeleton bone index, -1 = all
        float minWeight = 0.f;
        /** @brief "xy" | "xz" | "yz" */
        std::string plane = "xy";
        float scale = 1.f;
        bool enabled = false;
        /** @brief Vertex indices eligible for sampling (rebuilt when filter changes). */
        std::vector<int> candidates;
        bool candidatesDirty = true;
        int lastSkinnedFrame = -1;
    };

    /** @brief Pooled Light2D entities driven by ParticleLightSystem (lights.enabled). */
    struct Lights {
        std::vector<graphics::Light2D *> pool;
    };

    /** @brief Backend-owned GPU-resident simulation state and delayed CPU counters. */
    struct GpuSim {
        bool enabled = false;
        bool failed = false;
        /** @brief Backend-owned resident emitter; zero until the Vulkan path activates. */
        std::uint64_t residentHandle = 0;
        /** @brief True after state has moved to the resident GPU backend. */
        bool residentActive = false;
        /** @brief CPU-side exact lifetime count used for budgets; positions remain GPU-only. */
        int estimatedAlive = 0;
        /** @brief Monotonic simulated time used by the lifetime min-heap. */
        double timeline = 0.0;
        /** @brief Absolute death times; min-heap with std::greater<float>. */
        std::vector<float> deathTimes;
    };

    COMPONENT(Config, config)
    COMPONENT(Sim, sim)
    COMPONENT(Draw, draw)
    COMPONENT(Resource, resource)
    COMPONENT(Attach, attach)
    COMPONENT(SkinSource, skinSource)
    COMPONENT(Lights, lights)
    COMPONENT(GpuSim, gpuSim)

    static ParticleEmitter *createEmitter(int bufferSize = 1000);

    void setPosition(float x, float y);
    void moveTo(float x, float y);
    float getX();
    float getY();

    void setEmissionRate(float rate);
    float getEmissionRate();
    /** @brief Set particles emitted per world unit travelled; zero disables it. */
    void setEmissionRateOverDistance(float rate);
    /** @brief Return particles emitted per world unit travelled. */
    float getEmissionRateOverDistance();

    void setParticleLifetime(float minLife, float maxLife);
    float getParticleLifetimeMin();
    float getParticleLifetimeMax();

    void setEmitterLifetime(float seconds);
    float getEmitterLifetime();
    /** @brief Enable or disable repeating a finite emitter timeline. */
    void setLooping(bool looping);
    /** @brief Return whether the finite emitter timeline repeats. */
    bool getLooping();
    /** @brief Scale simulation time; zero freezes playback. */
    void setPlaybackSpeed(float speed);
    /** @brief Return the simulation time multiplier. */
    float getPlaybackSpeed();
    /** @brief Configure fixed stepping and the per-frame catch-up limit; zero seconds disables it. */
    void setFixedTimeStep(float seconds, int maxSubSteps = 8);
    /** @brief Return the fixed step in seconds, or zero for variable stepping. */
    float getFixedTimeStep();
    /** @brief Select a deterministic seed and disable automatic seeding. */
    void setRandomSeed(int seed);
    /** @brief Return the configured deterministic seed. */
    int getRandomSeed();
    /** @brief Generate a new random seed whenever start() is called. */
    void setAutoRandomSeed(bool enabled);
    /** @brief Return whether start() generates a new random seed. */
    bool getAutoRandomSeed();

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
    void setStartRotation(float minDeg, float maxDeg);

    void addBurst(float time, int count);
    void clearBursts();
    void setPrewarm(float seconds);
    float getPrewarmSeconds();

    void setGravity(float x, float y);
    void setDamping(float perSecond);
    void setLimitVelocity(float maxSpeed);
    void clearVelocityCurve();
    void addVelocityCurvePoint(float t, float v);
    void setInheritVelocity(float fraction);
    void setSimulationSpace(const std::string &space);
    void setNoise(float strength, float frequency = 1.f, float speed = 1.f);
    void setGpuSimulation(bool enable);
    bool getGpuSimulation();
    /** @brief Return true after this emitter has migrated to a resident GPU backend. */
    bool isGpuSimulationActive();

    /** @brief Set budget order; higher values are processed first. */
    void setPriority(int priority);
    /** @brief Return the emitter budget priority. */
    int getPriority();
    /** @brief Set the minimum runtime quality tier in [0,3]. */
    void setMinimumQuality(int quality);
    /** @brief Return the minimum runtime quality tier. */
    int getMinimumQuality();
    /** @brief Set offscreen simulation policy: automatic, pause, or always. */
    void setCullingMode(const std::string &mode);
    /** @brief Return the offscreen simulation policy. */
    std::string getCullingMode();
    /** @brief Set camera distance cutoff in world units; zero disables it. */
    void setCullDistance(float distance);
    /** @brief Return the camera distance cutoff. */
    float getCullDistance();
    /** @brief Cap automatic spawns per frame; zero is unlimited. */
    void setMaxSpawnPerFrame(int count);
    /** @brief Return the automatic per-frame spawn cap. */
    int getMaxSpawnPerFrame();

    void setCollision(const std::string &mode, float radius = 0.f, float restitution = 0.6f,
                      float lifetimeLoss = 0.f);
    void setCollisionBounds(bool enabled, float minX, float minY, float maxX, float maxY);
    void setWorldCollision(bool enabled);

    void setRenderMode(const std::string &mode, float stretchFactor = 1.f);
    /** @brief Configure connected ribbon rendering and switch to ribbon mode. */
    void setRibbon(float width = 1.f, float minSegmentLength = 1.f);
    /** @brief Fade resident particles where they intersect sampled scene depth. */
    void setSoftParticles(bool enabled, float particleDepth = 0.5f, float fadeDistance = 0.05f);
    /** @brief True when soft particles have both a resident renderer and scene depth. */
    bool isSoftParticlesActive();
    /** @brief Set the fixed screen-space orientation used by render mode "axis". */
    void setRenderAxis(float degrees);
    /** @brief Select stable per-emitter transparent ordering. */
    void setSortMode(const std::string& mode);
    /** @brief Return the configured transparent ordering policy. */
    std::string getSortMode();
    /** @brief Select particle shading: "unlit" or "lit". Lit mode uses scene 2D lights. */
    void setMaterialMode(const std::string& mode);
    /** @brief Return the normalized particle shading mode. */
    std::string getMaterialMode();
    void setOverflowMode(const std::string &mode);
    void setMaxDeltaTime(float seconds);

    void addSubEmitter(ParticleEmitter *target, const std::string &trigger,
                       float inheritVelocity = 0.f);
    void clearSubEmitters();

    void addForceField(float x, float y, float radius, float strength, float falloff = 1.f);
    void clearForceFields();

    void setShader(graphics::Shader *shader);
    graphics::Shader *getShader();

    void setLights(bool enabled, float radius = 120.f, float intensity = 1.f, float r = 1.f,
                   float g = 1.f, float b = 1.f, int maxLights = 4);
    bool getLightsEnabled();

    void setBlendMode(const std::string &mode);
    std::string getBlendMode();

    void setFlipbook(int hframes, int vframes, float framesPerSecond = 0.f,
                     float randomStart = 0.f);

    void clearColorGradient();
    void addColorStop(float t, float r, float g, float b, float a);
    void clearSizeCurve();
    void addSizeCurvePoint(float t, float v);
    void clearRotationCurve();
    void addRotationCurvePoint(float t, float v);

    void setColorStart(float r, float g, float b, float a = 1.f);
    void setColorEnd(float r, float g, float b, float a = 1.f);

    void setTexture(graphics::Texture *texture);
    graphics::Texture *getTexture();
    /** @brief Set an optional tangent-space normal map for lit particles. */
    void setNormalTexture(graphics::Texture* texture);
    /** @brief Return the configured particle normal map. */
    graphics::Texture* getNormalTexture();

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

    /** @brief Named preset: "spark" / "smoke" / "fire". Unknown → no-op. */
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
    /** @brief "none" | "anim" | "spine" | "ik2d" | "ik3d" */
    std::string getAttachKind();
    /** @brief Sync Config.x/y (and direction) from the attached bone. Also called by ParticleSimSystem. */
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
    /** @brief Burst-emit `count` particles from the current skinned surface. */
    void emitFromSkin(int count);
};

bool spawnParticle(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim);
bool spawnParticleAt(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float x, float y);
void stepEmitterSim(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float dt);
/** @brief Apply playback speed and optional bounded fixed stepping before simulation. */
float advanceEmitterSim(ParticleEmitter::Config& cfg, ParticleEmitter::Sim& sim, float dt);
/** @brief World collision query used by emitters with worldCollision enabled. */
using WorldCollisionFn = bool (*)(float x, float y, float radius, float &nx, float &ny);
void setWorldCollisionResolver(WorldCollisionFn fn);
WorldCollisionFn getWorldCollisionResolver();
/** @brief Sync bone attach + refresh skin cache; call before stepEmitterSim when using Attach/SkinSource. */
void syncEmitterSources(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim,
                        ParticleEmitter::Attach &attach, ParticleEmitter::SkinSource &skinSrc);
bool sampleSkinSpawn(ParticleEmitter::SkinSource &skinSrc, ParticleEmitter::Sim &sim, float &outX,
                     float &outY);

}  // namespace eve::particles
