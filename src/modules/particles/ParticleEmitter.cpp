#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"

#include "animation/AnimMath.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "particles/ParticleGpuKernel.h"

#include <cmath>
#include <cstdio>
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

float hashNoise2(int ix, int iy) {
    unsigned h = unsigned(ix) * 374761393u + unsigned(iy) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float((h ^ (h >> 16)) & 0xFFFFFFu) / float(0x1000000u)) * 2.f - 1.f;
}

/** Smooth 2D value noise in roughly [-1, 1] (cheap CPU turbulence). */
float smoothNoise2(float x, float y) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.f - 2.f * fx);
    const float sy = fy * fy * (3.f - 2.f * fy);
    const float n00 = hashNoise2(ix, iy);
    const float n10 = hashNoise2(ix + 1, iy);
    const float n01 = hashNoise2(ix, iy + 1);
    const float n11 = hashNoise2(ix + 1, iy + 1);
    const float a = n00 + (n10 - n00) * sx;
    const float b = n01 + (n11 - n01) * sx;
    return a + (b - a) * sy;
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
    } else if (cfg.areaType == "line") {
        ox = randRange(sim.rng, -cfg.areaX, cfg.areaX);
        oy = 0.f;
    } else if (cfg.areaType == "ring") {
        const float a = randRange(sim.rng, 0.f, kPi * 2.f);
        const float rx = cfg.areaX > 0.f ? cfg.areaX : 1.f;
        const float ry = cfg.areaY > 0.f ? cfg.areaY : rx;
        ox = std::cos(a) * rx;
        oy = std::sin(a) * ry;
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
    p.rot = randRange(sim.rng, cfg.startRotMin, cfg.startRotMax) * (kPi / 180.f);

    const int totalFrames = cfg.hframes > 0 && cfg.vframes > 0 ? cfg.hframes * cfg.vframes : 1;
    if (cfg.frameRandomStart > 0.f && totalFrames > 1) {
        const float frac = cfg.frameRandomStart > 1.f ? 1.f : cfg.frameRandomStart;
        p.frame = randRange(sim.rng, 0.f, frac) * float(totalFrames);
    } else {
        p.frame = 0.f;
    }
    p.noisePhase = randRange(sim.rng, 0.f, 1000.f);

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

void clearAttachSources(ParticleEmitter::Attach &a) {
    a.kind = ParticleEmitter::Attach::Kind::None;
    a.pose = nullptr;
    a.skeleton = nullptr;
    a.spine = nullptr;
    a.ik2d = nullptr;
    a.ik3d = nullptr;
    a.boneIndex = -1;
    a.enabled = false;
}

void syncAnimPoseAttach(ParticleEmitter::Config &cfg, ParticleEmitter::Attach &attach) {
    if (!attach.pose || attach.boneIndex < 0 || attach.boneIndex >= attach.pose->getBoneCount())
        return;
    const auto &w = attach.pose->world(attach.boneIndex);
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

void syncSpineAttach(ParticleEmitter::Config &cfg, ParticleEmitter::Attach &attach) {
    if (!attach.spine || attach.boneIndex < 0 || attach.boneIndex >= attach.spine->getBoneCount())
        return;
    float a, b, c, d;
    attach.spine->getBoneWorldMatrix(attach.boneIndex, a, b, c, d);
    const float wx =
        attach.spine->getBoneWorldX(attach.boneIndex) + a * attach.offsetX + b * attach.offsetY;
    const float wy =
        attach.spine->getBoneWorldY(attach.boneIndex) + c * attach.offsetX + d * attach.offsetY;
    // Spine is already 2D pixel space; scale still applies, plane ignored (xy).
    cfg.x = wx * attach.scale;
    cfg.y = wy * attach.scale;

    if (attach.followRotation) {
        // World rotation is degrees → particle direction radians.
        cfg.direction = attach.spine->getBoneWorldRotation(attach.boneIndex) * (kPi / 180.f);
    }
}

void syncIk2DAttach(ParticleEmitter::Config &cfg, ParticleEmitter::Attach &attach) {
    if (!attach.ik2d || attach.boneIndex < 0 || attach.boneIndex >= attach.ik2d->getBoneCount())
        return;
    float ox = attach.offsetX;
    float oy = attach.offsetY;
    if (attach.followRotation || (ox != 0.f || oy != 0.f)) {
        const float fx = attach.ik2d->getOrientationX(attach.boneIndex);
        const float fy = attach.ik2d->getOrientationY(attach.boneIndex);
        const float len2 = fx * fx + fy * fy;
        if (len2 > kEps) {
            const float inv = 1.f / std::sqrt(len2);
            const float ux = fx * inv;
            const float uy = fy * inv;
            // Local +X along bone forward, +Y perpendicular.
            const float rx = ox * ux - oy * uy;
            const float ry = ox * uy + oy * ux;
            ox = rx;
            oy = ry;
            if (attach.followRotation) cfg.direction = std::atan2(uy, ux);
        }
    }
    cfg.x = (attach.ik2d->getX(attach.boneIndex) + ox) * attach.scale;
    cfg.y = (attach.ik2d->getY(attach.boneIndex) + oy) * attach.scale;
}

void syncIk3DAttach(ParticleEmitter::Config &cfg, ParticleEmitter::Attach &attach) {
    if (!attach.ik3d || attach.boneIndex < 0 || attach.boneIndex >= attach.ik3d->getBoneCount())
        return;
    float ox = attach.offsetX;
    float oy = attach.offsetY;
    float oz = attach.offsetZ;
    const float fx = attach.ik3d->getOrientationX(attach.boneIndex);
    const float fy = attach.ik3d->getOrientationY(attach.boneIndex);
    const float fz = attach.ik3d->getOrientationZ(attach.boneIndex);
    const float len2 = fx * fx + fy * fy + fz * fz;
    if (len2 > kEps && (ox != 0.f || oy != 0.f || oz != 0.f || attach.followRotation)) {
        const float inv = 1.f / std::sqrt(len2);
        const float ux = fx * inv, uy = fy * inv, uz = fz * inv;
        // Offset: along-bone (ox) + world remainder (oy/oz as translation extras).
        const float wx = attach.ik3d->getX(attach.boneIndex) + ux * ox + oy;
        const float wy = attach.ik3d->getY(attach.boneIndex) + uy * ox + oz;
        const float wz = attach.ik3d->getZ(attach.boneIndex) + uz * ox;
        projectToPlane(wx, wy, wz, attach.plane, attach.scale, cfg.x, cfg.y);
        if (attach.followRotation) {
            float ax, ay;
            projectToPlane(ux, uy, uz, attach.plane, 1.f, ax, ay);
            if (ax * ax + ay * ay > kEps) cfg.direction = std::atan2(ay, ax);
        }
        return;
    }
    projectToPlane(attach.ik3d->getX(attach.boneIndex) + ox, attach.ik3d->getY(attach.boneIndex) + oy,
                   attach.ik3d->getZ(attach.boneIndex) + oz, attach.plane, attach.scale, cfg.x,
                   cfg.y);
}

void fireSubEmitter(ParticleEmitter::Config &cfg, const std::string &trigger, float x, float y,
                    float vx, float vy) {
    static int g_subDepth = 0;
    if (g_subDepth >= 8) return;  // cycle guard (A→B→A chains)
    for (const auto &se : cfg.subEmitters) {
        if (!se.target || se.trigger != trigger) continue;
        // Self-referencing sub-emitters would corrupt the compaction loop.
        if (se.target == cfg.entity) continue;
        auto tc = se.target->config();
        auto ts = se.target->sim();
        if (ts->alive >= int(ts->particles.size())) continue;
        ++g_subDepth;
        spawnParticleAt(*tc, *ts, x, y);
        --g_subDepth;
        if (se.inheritVelocity > 0.f) {
            Particle &p = ts->particles[size_t(ts->alive - 1)];
            p.vx += vx * se.inheritVelocity;
            p.vy += vy * se.inheritVelocity;
        }
    }
}

}  // namespace

namespace {
WorldCollisionFn g_worldCollision = nullptr;
}  // namespace

void setWorldCollisionResolver(WorldCollisionFn fn) { g_worldCollision = fn; }

WorldCollisionFn getWorldCollisionResolver() { return g_worldCollision; }

void spawnParticleAt(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float x, float y) {
    if (sim.alive >= int(sim.particles.size())) return;

    Particle &p = sim.particles[size_t(sim.alive++)];
    float ox = 0.f, oy = 0.f;
    sampleEmissionOffset(cfg, sim, ox, oy);
    p.x = x + ox;
    p.y = y + oy;
    fillParticleMotion(cfg, sim, p);
    fireSubEmitter(cfg, "birth", p.x, p.y, p.vx, p.vy);
}

void spawnParticle(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim) {
    // Prefer skin surface when configured on the owning entity.
    if (cfg.entity) {
        auto skinComp = cfg.entity->skinSource();
        if (skinComp->enabled) {
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
    if (attach.enabled) {
        switch (attach.kind) {
            case ParticleEmitter::Attach::Kind::AnimPose:
                syncAnimPoseAttach(cfg, attach);
                break;
            case ParticleEmitter::Attach::Kind::Spine:
                syncSpineAttach(cfg, attach);
                break;
            case ParticleEmitter::Attach::Kind::Ik2D:
                syncIk2DAttach(cfg, attach);
                break;
            case ParticleEmitter::Attach::Kind::Ik3D:
                syncIk3DAttach(cfg, attach);
                break;
            case ParticleEmitter::Attach::Kind::None:
            default:
                break;
        }
    }

    if (skinSrc.enabled && skinSrc.skin && skinSrc.pose) {
        ensureSkinCache(skinSrc);
    }
}

void stepEmitterSim(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float dt) {
    if (sim.paused || dt <= 0.f) return;
    if (cfg.maxDeltaTime > 0.f && dt > cfg.maxDeltaTime) dt = cfg.maxDeltaTime;

    // Emitter velocity for inheritVelocity (before lastX/lastY refresh).
    float emitVx = 0.f, emitVy = 0.f;
    if (cfg.inheritVelocity > 0.f && sim.hasLastPos) {
        emitVx = (cfg.x - sim.lastX) / dt;
        emitVy = (cfg.y - sim.lastY) / dt;
    }
    const bool localSpace = cfg.simSpace == "local";
    const float localDx = localSpace && sim.hasLastPos ? cfg.x - sim.lastX : 0.f;
    const float localDy = localSpace && sim.hasLastPos ? cfg.y - sim.lastY : 0.f;

    const float damp = cfg.damping > 0.f ? (cfg.damping > 1.f ? 1.f : cfg.damping) : 0.f;
    const float dampFactor = damp > 0.f ? std::max(0.f, 1.f - damp * dt) : 1.f;
    const float noiseFreq = cfg.noiseFrequency > 0.f ? cfg.noiseFrequency : 1.f;
    const bool hasCollision =
        cfg.collisionMode != "none" && (cfg.worldCollision || cfg.collisionBoundsEnabled);
    const bool bounce = cfg.collisionMode == "bounce";
    const bool killMode = cfg.collisionMode == "kill";
    const bool stopMode = cfg.collisionMode == "stop";
    const float cRadius = cfg.collisionRadius;
    const float lifeLoss = cfg.collisionLifetimeLoss;

    int write = 0;
    for (int i = 0; i < sim.alive; ++i) {
        Particle &p = sim.particles[size_t(i)];
        p.life -= dt;
        if (p.life <= 0.f) {
            fireSubEmitter(cfg, "death", p.x, p.y, p.vx, p.vy);
            continue;
        }

        if (localSpace && (localDx != 0.f || localDy != 0.f)) {
            p.x += localDx;
            p.y += localDy;
        }

        float ax = p.ax + cfg.gravityX;
        float ay = p.ay + cfg.gravityY;
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

        if (cfg.noiseStrength != 0.f) {
            const float t = sim.emitterAge * cfg.noiseSpeed;
            ax += smoothNoise2(p.x * noiseFreq + t, p.y * noiseFreq + p.noisePhase) *
                  cfg.noiseStrength;
            ay += smoothNoise2(p.y * noiseFreq + t, p.x * noiseFreq - p.noisePhase) *
                  cfg.noiseStrength;
        }

        // Radial force fields (strength > 0 attract, < 0 repel).
        for (const auto &f : cfg.forceFields) {
            if (f.radius <= 0.f || f.strength == 0.f) continue;
            const float fdx = f.x - p.x;
            const float fdy = f.y - p.y;
            const float dist = std::sqrt(fdx * fdx + fdy * fdy);
            if (dist >= f.radius) continue;
            const float fall = std::pow(1.f - dist / f.radius, f.falloff > 0.f ? f.falloff : 1.f);
            const float inv = dist > kEps ? 1.f / dist : 0.f;
            ax += fdx * inv * f.strength * fall;
            ay += fdy * inv * f.strength * fall;
        }

        p.vx += ax * dt;
        p.vy += ay * dt;

        // Velocity-over-lifetime multiplier (applied as a smooth ratio).
        if (!cfg.velocityCurve.empty() && p.lifetime > 0.f) {
            const float tNew = 1.f - (p.life / p.lifetime);
            const float tOld = tNew - dt / p.lifetime;
            const float vOld = cfg.velocityCurve.sample(tOld, 1.f);
            const float vNew = cfg.velocityCurve.sample(tNew, 1.f);
            if (vOld > 1e-4f) {
                const float ratio = vNew / vOld;
                p.vx *= ratio;
                p.vy *= ratio;
            }
        }

        if (dampFactor != 1.f) {
            p.vx *= dampFactor;
            p.vy *= dampFactor;
        }
        if (cfg.limitVelocity > 0.f) {
            const float sp2 = p.vx * p.vx + p.vy * p.vy;
            const float max2 = cfg.limitVelocity * cfg.limitVelocity;
            if (sp2 > max2) {
                const float s = std::sqrt(max2 / sp2);
                p.vx *= s;
                p.vy *= s;
            }
        }

        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.spin * dt;
        p.frame += cfg.frameRate * dt;

        if (hasCollision) {
            const float scale = p.size > 0.f ? p.size : 1.f;
            const float rad =
                cRadius > 0.f ? cRadius : std::max(cfg.particleW, cfg.particleH) * 0.5f * scale;
            float nx = 0.f, ny = 0.f;
            bool hit = false;
            if (cfg.worldCollision) {
                if (WorldCollisionFn fn = getWorldCollisionResolver())
                    if (fn(p.x, p.y, rad, nx, ny)) hit = true;
            }
            if (!hit && cfg.collisionBoundsEnabled) {
                if (p.x - rad < cfg.boundsMinX) {
                    nx += 1.f;
                    p.x = cfg.boundsMinX + rad;
                    hit = true;
                } else if (p.x + rad > cfg.boundsMaxX) {
                    nx += -1.f;
                    p.x = cfg.boundsMaxX - rad;
                    hit = true;
                }
                if (p.y - rad < cfg.boundsMinY) {
                    ny += 1.f;
                    p.y = cfg.boundsMinY + rad;
                    hit = true;
                } else if (p.y + rad > cfg.boundsMaxY) {
                    ny += -1.f;
                    p.y = cfg.boundsMaxY - rad;
                    hit = true;
                }
                const float nlen = std::sqrt(nx * nx + ny * ny);
                if (nlen > kEps) {
                    nx /= nlen;
                    ny /= nlen;
                }
            }
            if (hit) {
                fireSubEmitter(cfg, "collision", p.x, p.y, p.vx, p.vy);
                if (killMode) continue;
                if (stopMode) {
                    p.vx = 0.f;
                    p.vy = 0.f;
                } else if (bounce && (nx != 0.f || ny != 0.f)) {
                    const float dot = p.vx * nx + p.vy * ny;
                    p.vx = (p.vx - 2.f * dot * nx) * cfg.collisionRestitution;
                    p.vy = (p.vy - 2.f * dot * ny) * cfg.collisionRestitution;
                }
                if (lifeLoss > 0.f) {
                    p.life -= p.lifetime * lifeLoss;
                    if (p.life <= 0.f) continue;
                }
            }
        }

        if (write != i) sim.particles[size_t(write)] = p;
        ++write;
    }
    sim.alive = write;

    if (!sim.active) {
        sim.lastX = cfg.x;
        sim.lastY = cfg.y;
        sim.hasLastPos = true;
        return;
    }

    sim.emitterAge += dt;

    // Timed bursts fire once while the emitter is active.
    auto spawnWithInherit = [&]() {
        spawnParticle(cfg, sim);
        if (cfg.inheritVelocity > 0.f && sim.alive > 0) {
            Particle &np = sim.particles[size_t(sim.alive - 1)];
            np.vx += emitVx * cfg.inheritVelocity;
            np.vy += emitVy * cfg.inheritVelocity;
        }
    };
    for (auto &b : cfg.bursts) {
        if (!b.emitted && b.count > 0 && sim.emitterAge >= b.time) {
            b.emitted = true;
            for (int k = 0; k < b.count; ++k) {
                if (sim.alive >= int(sim.particles.size())) break;
                spawnWithInherit();
            }
        }
    }

    if (cfg.emissionRate > 0.f) {
        sim.emitAccum += cfg.emissionRate * dt;
        while (sim.emitAccum >= 1.f) {
            if (sim.alive >= int(sim.particles.size())) {
                if (cfg.overflowMode == "pause") break;  // keep accum; retry next frame
                if (cfg.overflowMode == "warn" && !sim.overflowWarned) {
                    sim.overflowWarned = true;
                    std::fprintf(stderr,
                                 "[particles] buffer overflow (emissionRate=%.1f, buffer=%d)\n",
                                 cfg.emissionRate, int(sim.particles.size()));
                }
                sim.emitAccum = 0.f;
                break;
            }
            spawnWithInherit();
            sim.emitAccum -= 1.f;
        }
    }

    // Expire AFTER this frame's emission so a short-lived emitter still
    // releases the particles due during its final frame.
    if (cfg.emitterLife >= 0.f && sim.emitterAge >= cfg.emitterLife) {
        sim.active = false;
        sim.emitAccum = 0.f;
    }

    sim.lastX = cfg.x;
    sim.lastY = cfg.y;
    sim.hasLastPos = true;
}

namespace {

constexpr int kGpuStride = 16;

eve::gpgpu::ComputeShader *sharedGpuParticleShader(eve::gpgpu::Gpgpu *gpgpu) {
    static eve::gpgpu::ComputeShader *s_shader = nullptr;
    static eve::gpgpu::Gpgpu *s_gpgpu = nullptr;
    if (!s_shader || s_gpgpu != gpgpu) {
        delete s_shader;
        s_shader = nullptr;
        try {
            s_shader = gpgpu->newShader(kParticleGpuKernel);
        } catch (...) {
            s_shader = nullptr;
        }
        s_gpgpu = gpgpu;
    }
    return s_shader;
}

void packParticles(const ParticleEmitter::Sim &sim, std::vector<float> &mirror) {
    const size_t n = sim.particles.size();
    mirror.assign(n * size_t(kGpuStride), 0.f);
    for (size_t i = 0; i < n && int(i) < sim.alive; ++i) {
        const Particle &p = sim.particles[i];
        float *m = mirror.data() + i * size_t(kGpuStride);
        m[0] = p.x;
        m[1] = p.y;
        m[2] = p.vx;
        m[3] = p.vy;
        m[4] = p.life;
        m[5] = p.lifetime;
        m[6] = p.size;
        m[7] = p.rot;
        m[8] = p.spin;
        m[9] = p.frame;
        m[10] = p.radial;
        m[11] = p.tangential;
        m[12] = p.ax;
        m[13] = p.ay;
        m[14] = p.noisePhase;
    }
}

void unpackParticles(const std::vector<float> &mirror, ParticleEmitter::Sim &sim) {
    const size_t n = sim.particles.size();
    sim.alive = 0;
    for (size_t i = 0; i < n; ++i) {
        const float *m = mirror.data() + i * size_t(kGpuStride);
        Particle &p = sim.particles[i];
        p.x = m[0];
        p.y = m[1];
        p.vx = m[2];
        p.vy = m[3];
        p.life = m[4];
        p.lifetime = m[5];
        p.size = m[6];
        p.rot = m[7];
        p.spin = m[8];
        p.frame = m[9];
        p.radial = m[10];
        p.tangential = m[11];
        p.ax = m[12];
        p.ay = m[13];
        p.noisePhase = m[14];
        if (p.life > 0.f) ++sim.alive;
    }
}

}  // namespace

/**
 * GPU-accelerated variant of stepEmitterSim. The per-particle integration loop
 * runs in a compute shader; spawning / death compaction / collision stay on
 * the CPU. Returns false when the GPU path is unavailable — the caller then
 * falls back to stepEmitterSim.
 */
bool stepEmitterSimGpu(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim,
                       ParticleEmitter::GpuSim &gpu, float dt) {
    if (sim.paused || dt <= 0.f) return true;
    if (cfg.maxDeltaTime > 0.f && dt > cfg.maxDeltaTime) dt = cfg.maxDeltaTime;

    auto *gpgpu = eve::gpgpu::Gpgpu::create();
    if (!gpgpu || !gpgpu->isAvailable()) return false;

    if (!gpu.initialized) {
        gpu.initialized = true;
        const size_t n = sim.particles.size();
        gpu.mirror.assign(n * size_t(kGpuStride), 0.f);
        try {
            gpu.buffer.reset(
                gpgpu->newBuffer(int(n * size_t(kGpuStride) * sizeof(float)), "storage"));
        } catch (...) {
            gpu.failed = true;
            return false;
        }
        if (!gpu.buffer) {
            gpu.failed = true;
            return false;
        }
    }
    if (gpu.failed) return false;

    auto *shader = sharedGpuParticleShader(gpgpu);
    if (!shader) {
        gpu.failed = true;
        return false;
    }

    // Emitter velocity for inheritVelocity (before lastX/lastY refresh).
    float emitVx = 0.f, emitVy = 0.f;
    if (cfg.inheritVelocity > 0.f && sim.hasLastPos) {
        emitVx = (cfg.x - sim.lastX) / dt;
        emitVy = (cfg.y - sim.lastY) / dt;
    }
    const bool localSpace = cfg.simSpace == "local";
    const float localDx = localSpace && sim.hasLastPos ? cfg.x - sim.lastX : 0.f;
    const float localDy = localSpace && sim.hasLastPos ? cfg.y - sim.lastY : 0.f;

    // 1) Upload the current CPU state, 2) dispatch the integration kernel,
    // 3) read back the integrated state.
    packParticles(sim, gpu.mirror);
    try {
        gpu.buffer->writeFloat32s(gpu.mirror.data(), int(gpu.mirror.size()), 0);
        const int alive = sim.alive;
        if (alive > 0) {
            shader->bindBuffer(0, gpu.buffer.get());
            shader->setFloat(0, float(alive));
            shader->setFloat(1, dt);
            shader->setFloat(2, cfg.x);
            shader->setFloat(3, cfg.y);
            shader->setFloat(4, cfg.gravityX);
            shader->setFloat(5, cfg.gravityY);
            shader->setFloat(6, cfg.damping);
            shader->setFloat(7, cfg.limitVelocity);
            shader->setFloat(8, cfg.noiseStrength);
            shader->setFloat(9, cfg.noiseFrequency > 0.f ? cfg.noiseFrequency : 1.f);
            shader->setFloat(10, cfg.noiseSpeed);
            shader->setFloat(11, sim.emitterAge);
            shader->setFloat(12, cfg.frameRate);
            const int groups = (alive + 63) / 64;
            gpgpu->dispatch(shader, groups, 1, 1);
        }
        gpu.buffer->readFloat32s(gpu.mirror.data(), int(gpu.mirror.size()), 0);
    } catch (...) {
        gpu.failed = true;
        return false;
    }
    unpackParticles(gpu.mirror, sim);
    const int gpuAlive = sim.alive;

    // 4) CPU-side post-integration: local-space tracking, collision, death
    //    compaction (fire death sub-emitters), emitter age, bursts, emission.
    const bool hasCollision =
        cfg.collisionMode != "none" && (cfg.worldCollision || cfg.collisionBoundsEnabled);
    const bool bounce = cfg.collisionMode == "bounce";
    const bool killMode = cfg.collisionMode == "kill";
    const bool stopMode = cfg.collisionMode == "stop";
    const float cRadius = cfg.collisionRadius;
    const float lifeLoss = cfg.collisionLifetimeLoss;

    int write = 0;
    for (int i = 0; i < int(sim.particles.size()); ++i) {
        Particle &p = sim.particles[size_t(i)];
        if (p.life <= 0.f) {
            if (i < gpuAlive) fireSubEmitter(cfg, "death", p.x, p.y, p.vx, p.vy);
            continue;
        }
        if (localSpace && (localDx != 0.f || localDy != 0.f)) {
            p.x += localDx;
            p.y += localDy;
        }
        if (hasCollision) {
            const float scale = p.size > 0.f ? p.size : 1.f;
            const float rad =
                cRadius > 0.f ? cRadius : std::max(cfg.particleW, cfg.particleH) * 0.5f * scale;
            float nx = 0.f, ny = 0.f;
            bool hit = false;
            if (cfg.worldCollision) {
                if (WorldCollisionFn fn = getWorldCollisionResolver())
                    if (fn(p.x, p.y, rad, nx, ny)) hit = true;
            }
            if (!hit && cfg.collisionBoundsEnabled) {
                if (p.x - rad < cfg.boundsMinX) {
                    nx += 1.f;
                    p.x = cfg.boundsMinX + rad;
                    hit = true;
                } else if (p.x + rad > cfg.boundsMaxX) {
                    nx += -1.f;
                    p.x = cfg.boundsMaxX - rad;
                    hit = true;
                }
                if (p.y - rad < cfg.boundsMinY) {
                    ny += 1.f;
                    p.y = cfg.boundsMinY + rad;
                    hit = true;
                } else if (p.y + rad > cfg.boundsMaxY) {
                    ny += -1.f;
                    p.y = cfg.boundsMaxY - rad;
                    hit = true;
                }
                const float nlen = std::sqrt(nx * nx + ny * ny);
                if (nlen > kEps) {
                    nx /= nlen;
                    ny /= nlen;
                }
            }
            if (hit) {
                fireSubEmitter(cfg, "collision", p.x, p.y, p.vx, p.vy);
                if (killMode) continue;
                if (stopMode) {
                    p.vx = 0.f;
                    p.vy = 0.f;
                } else if (bounce && (nx != 0.f || ny != 0.f)) {
                    const float dot = p.vx * nx + p.vy * ny;
                    p.vx = (p.vx - 2.f * dot * nx) * cfg.collisionRestitution;
                    p.vy = (p.vy - 2.f * dot * ny) * cfg.collisionRestitution;
                }
                if (lifeLoss > 0.f) {
                    p.life -= p.lifetime * lifeLoss;
                    if (p.life <= 0.f) continue;
                }
            }
        }
        if (write != i) sim.particles[size_t(write)] = p;
        ++write;
    }
    sim.alive = write;
    // Mark stale tail slots dead so the next readback never re-fires death subs.
    for (int i = write; i < int(sim.particles.size()); ++i)
        sim.particles[size_t(i)].life = -1.f;

    if (!sim.active) {
        sim.lastX = cfg.x;
        sim.lastY = cfg.y;
        sim.hasLastPos = true;
        return true;
    }

    sim.emitterAge += dt;

    auto spawnWithInherit = [&]() {
        spawnParticle(cfg, sim);
        if (cfg.inheritVelocity > 0.f && sim.alive > 0) {
            Particle &np = sim.particles[size_t(sim.alive - 1)];
            np.vx += emitVx * cfg.inheritVelocity;
            np.vy += emitVy * cfg.inheritVelocity;
        }
    };
    for (auto &b : cfg.bursts) {
        if (!b.emitted && b.count > 0 && sim.emitterAge >= b.time) {
            b.emitted = true;
            for (int k = 0; k < b.count; ++k) {
                if (sim.alive >= int(sim.particles.size())) break;
                spawnWithInherit();
            }
        }
    }
    if (cfg.emissionRate > 0.f) {
        sim.emitAccum += cfg.emissionRate * dt;
        while (sim.emitAccum >= 1.f) {
            if (sim.alive >= int(sim.particles.size())) {
                if (cfg.overflowMode == "pause") break;
                if (cfg.overflowMode == "warn" && !sim.overflowWarned) {
                    sim.overflowWarned = true;
                    std::fprintf(stderr,
                                 "[particles] buffer overflow (emissionRate=%.1f, buffer=%d)\n",
                                 cfg.emissionRate, int(sim.particles.size()));
                }
                sim.emitAccum = 0.f;
                break;
            }
            spawnWithInherit();
            sim.emitAccum -= 1.f;
        }
    }

    // Expire AFTER this frame's emission (see stepEmitterSim).
    if (cfg.emitterLife >= 0.f && sim.emitterAge >= cfg.emitterLife) {
        sim.active = false;
        sim.emitAccum = 0.f;
    }

    sim.lastX = cfg.x;
    sim.lastY = cfg.y;
    sim.hasLastPos = true;
    return true;
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
    (void)e->lights();
    (void)e->gpuSim();
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
    if (type == "ellipse" || type == "rect" || type == "line" || type == "ring")
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

void ParticleEmitter::setStartRotation(float minDeg, float maxDeg) {
    auto c = config();
    c->startRotMin = minDeg;
    c->startRotMax = maxDeg < minDeg ? minDeg : maxDeg;
}

void ParticleEmitter::addBurst(float time, int count) {
    if (count <= 0) return;
    config()->bursts.push_back(Config::Burst{time < 0.f ? 0.f : time, count, false});
}

void ParticleEmitter::clearBursts() { config()->bursts.clear(); }

void ParticleEmitter::setPrewarm(float seconds) {
    config()->prewarmSeconds = seconds < 0.f ? 0.f : seconds;
}

float ParticleEmitter::getPrewarmSeconds() { return config()->prewarmSeconds; }

void ParticleEmitter::setGravity(float x, float y) {
    auto c = config();
    c->gravityX = x;
    c->gravityY = y;
}

void ParticleEmitter::setDamping(float perSecond) {
    config()->damping = perSecond < 0.f ? 0.f : perSecond;
}

void ParticleEmitter::setLimitVelocity(float maxSpeed) {
    config()->limitVelocity = maxSpeed < 0.f ? 0.f : maxSpeed;
}

void ParticleEmitter::clearVelocityCurve() { config()->velocityCurve.clear(); }

void ParticleEmitter::addVelocityCurvePoint(float t, float v) {
    config()->velocityCurve.add(t, v);
}

void ParticleEmitter::setInheritVelocity(float fraction) {
    config()->inheritVelocity =
        fraction < 0.f ? 0.f : (fraction > 1.f ? 1.f : fraction);
}

void ParticleEmitter::setSimulationSpace(const std::string &space) {
    config()->simSpace = space == "local" ? "local" : "world";
}

void ParticleEmitter::setNoise(float strength, float frequency, float speed) {
    auto c = config();
    c->noiseStrength = strength;
    c->noiseFrequency = frequency > 0.f ? frequency : 1.f;
    c->noiseSpeed = speed;
}

void ParticleEmitter::setGpuSimulation(bool enable) {
    config()->gpuSimulation = enable;
    gpuSim()->enabled = enable;
}

bool ParticleEmitter::getGpuSimulation() { return config()->gpuSimulation; }

void ParticleEmitter::setCollision(const std::string &mode, float radius, float restitution,
                                   float lifetimeLoss) {
    auto c = config();
    c->collisionMode =
        (mode == "kill" || mode == "bounce" || mode == "stop") ? mode : "none";
    c->collisionRadius = radius < 0.f ? 0.f : radius;
    c->collisionRestitution = restitution < 0.f ? 0.f : restitution;
    c->collisionLifetimeLoss =
        lifetimeLoss < 0.f ? 0.f : (lifetimeLoss > 1.f ? 1.f : lifetimeLoss);
}

void ParticleEmitter::setCollisionBounds(bool enabled, float minX, float minY, float maxX,
                                         float maxY) {
    auto c = config();
    c->collisionBoundsEnabled = enabled;
    c->boundsMinX = minX;
    c->boundsMinY = minY;
    c->boundsMaxX = maxX;
    c->boundsMaxY = maxY;
}

void ParticleEmitter::setWorldCollision(bool enabled) { config()->worldCollision = enabled; }

void ParticleEmitter::setRenderMode(const std::string &mode, float stretchFactor) {
    auto c = config();
    c->renderMode = mode == "stretched" ? "stretched" : "billboard";
    c->stretchFactor = stretchFactor < 0.f ? 0.f : stretchFactor;
}

void ParticleEmitter::setOverflowMode(const std::string &mode) {
    config()->overflowMode =
        (mode == "pause" || mode == "warn") ? mode : "drop";
}

void ParticleEmitter::setMaxDeltaTime(float seconds) {
    config()->maxDeltaTime = seconds < 0.f ? 0.f : seconds;
}

void ParticleEmitter::addSubEmitter(ParticleEmitter *target, const std::string &trigger,
                                    float inheritVelocity) {
    if (!target || target == this) return;
    Config::SubEmitter se;
    se.target = target;
    se.trigger = (trigger == "death" || trigger == "collision") ? trigger : "birth";
    se.inheritVelocity = inheritVelocity < 0.f ? 0.f : (inheritVelocity > 1.f ? 1.f : inheritVelocity);
    config()->subEmitters.push_back(se);
}

void ParticleEmitter::clearSubEmitters() { config()->subEmitters.clear(); }

void ParticleEmitter::addForceField(float x, float y, float radius, float strength,
                                    float falloff) {
    if (radius <= 0.f || strength == 0.f) return;
    Config::ForceField f;
    f.x = x;
    f.y = y;
    f.radius = radius;
    f.strength = strength;
    f.falloff = falloff > 0.f ? falloff : 1.f;
    config()->forceFields.push_back(f);
}

void ParticleEmitter::clearForceFields() { config()->forceFields.clear(); }

void ParticleEmitter::setShader(graphics::Shader *shader) { draw()->shader = shader; }
graphics::Shader *ParticleEmitter::getShader() { return draw()->shader; }

void ParticleEmitter::setLights(bool enabled, float radius, float intensity, float r, float g,
                                float b, int maxLights) {
    auto c = config();
    c->lights.enabled = enabled;
    c->lights.radius = radius > 0.f ? radius : 0.f;
    c->lights.intensity = intensity;
    c->lights.r = r;
    c->lights.g = g;
    c->lights.b = b;
    c->lights.max = maxLights > 0 ? maxLights : 0;
}

bool ParticleEmitter::getLightsEnabled() { return config()->lights.enabled; }

void ParticleEmitter::setBlendMode(const std::string &mode) {
    if (mode == "additive")
        draw()->blend = BlendMode::Additive;
    else if (mode == "opaque")
        draw()->blend = BlendMode::Opaque;
    else if (mode == "premultiplied" || mode == "premultiplied_alpha")
        draw()->blend = BlendMode::Premultiplied;
    else if (mode == "multiply")
        draw()->blend = BlendMode::Multiply;
    else
        draw()->blend = BlendMode::Alpha;
}

std::string ParticleEmitter::getBlendMode() {
    switch (draw()->blend) {
        case BlendMode::Additive:
            return "additive";
        case BlendMode::Opaque:
            return "opaque";
        case BlendMode::Premultiplied:
            return "premultiplied";
        case BlendMode::Multiply:
            return "multiply";
        case BlendMode::Alpha:
        default:
            return "alpha";
    }
}

void ParticleEmitter::setFlipbook(int h, int v, float framesPerSecond, float randomStart) {
    auto c = config();
    c->hframes = h > 0 ? h : 1;
    c->vframes = v > 0 ? v : 1;
    c->frameRate = framesPerSecond;
    c->frameRandomStart = randomStart < 0.f ? 0.f : (randomStart > 1.f ? 1.f : randomStart);
}

void ParticleEmitter::clearColorGradient() { config()->colorGradient.clear(); }

void ParticleEmitter::addColorStop(float t, float r, float g, float b, float a) {
    config()->colorGradient.add(t, r, g, b, a);
}

void ParticleEmitter::clearSizeCurve() { config()->sizeCurve.clear(); }

void ParticleEmitter::addSizeCurvePoint(float t, float v) { config()->sizeCurve.add(t, v); }

void ParticleEmitter::clearRotationCurve() { config()->rotationCurve.clear(); }

void ParticleEmitter::addRotationCurvePoint(float t, float v) {
    config()->rotationCurve.add(t, v);
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
    for (auto &b : config()->bursts) b.emitted = false;
    const float prewarm = config()->prewarmSeconds;
    if (prewarm > 0.f) {
        constexpr float kPrewarmDt = 1.f / 60.f;
        const int steps = int(std::ceil(prewarm / kPrewarmDt));
        for (int i = 0; i < steps; ++i) stepEmitterSim(*config(), *s, kPrewarmDt);
    }
}

void ParticleEmitter::stop() {
    auto s = sim();
    s->active = false;
    s->paused = false;
    s->emitAccum = 0.f;
    s->emitterAge = 0.f;
    s->lastX = config()->x;
    s->lastY = config()->y;
    s->hasLastPos = true;
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
    s->hasLastPos = false;
    s->overflowWarned = false;
    for (auto &b : config()->bursts) b.emitted = false;
    for (auto &p : s->particles) p.life = 0.f;
    auto g = gpuSim();
    if (g->buffer) {
        try {
            g->buffer->fillFloat32(0.f);
        } catch (...) {
        }
    }
    if (!g->mirror.empty()) std::fill(g->mirror.begin(), g->mirror.end(), 0.f);
}

void ParticleEmitter::emit(int count) {
    if (count <= 0) return;
    auto c = config();
    auto s = sim();
    // Spawn at the CURRENT attached position: refresh bone/skin sync so a
    // script that moves the pose then emits gets the new origin (no one-frame
    // lag). No-op for unattached emitters.
    syncAttach();
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
    auto a = attach();
    clearAttachSources(*a);
    a->kind = Attach::Kind::AnimPose;
    a->pose = pose;
    a->boneIndex = boneIndex;
    a->enabled = pose != nullptr && boneIndex >= 0;
    if (!a->enabled) a->kind = Attach::Kind::None;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::attachToBoneByName(animation::AnimPose *pose,
                                         animation::AnimSkeleton *skeleton,
                                         const std::string &boneName) {
    auto a = attach();
    a->skeleton = skeleton;
    int idx = -1;
    if (skeleton) idx = skeleton->findBone(boneName);
    attachToBone(pose, idx);
    // Preserve skeleton pointer for name lookups after attachToBone cleared sources.
    attach()->skeleton = skeleton;
}

void ParticleEmitter::attachToSpineBone(animation::SpineSkeleton *spine, int boneIndex) {
    auto a = attach();
    clearAttachSources(*a);
    a->kind = Attach::Kind::Spine;
    a->spine = spine;
    a->boneIndex = boneIndex;
    a->enabled = spine != nullptr && boneIndex >= 0 && boneIndex < spine->getBoneCount();
    if (!a->enabled) a->kind = Attach::Kind::None;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::attachToSpineBoneByName(animation::SpineSkeleton *spine,
                                              const std::string &boneName) {
    int idx = -1;
    if (spine && spine->getData()) idx = spine->getData()->findBone(boneName);
    attachToSpineBone(spine, idx);
}

void ParticleEmitter::attachToSkeleton2D(eve::ik::Skeleton2D *skeleton, int boneId) {
    auto a = attach();
    clearAttachSources(*a);
    a->kind = Attach::Kind::Ik2D;
    a->ik2d = skeleton;
    a->boneIndex = boneId;
    a->enabled = skeleton != nullptr && boneId >= 0 && boneId < skeleton->getBoneCount();
    if (!a->enabled) a->kind = Attach::Kind::None;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::attachToSkeleton3D(eve::ik::Skeleton3D *skeleton, int boneId) {
    auto a = attach();
    clearAttachSources(*a);
    a->kind = Attach::Kind::Ik3D;
    a->ik3d = skeleton;
    a->boneIndex = boneId;
    a->enabled = skeleton != nullptr && boneId >= 0 && boneId < skeleton->getBoneCount();
    if (!a->enabled) a->kind = Attach::Kind::None;
    if (a->enabled) syncAttach();
}

void ParticleEmitter::setAttachOffset(float x, float y, float z) {
    auto a = attach();
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
    clearAttachSources(*attach());
}

bool ParticleEmitter::isAttached() { return attach()->enabled; }
int ParticleEmitter::getAttachBone() { return attach()->boneIndex; }

std::string ParticleEmitter::getAttachKind() {
    switch (attach()->kind) {
        case Attach::Kind::AnimPose:
            return "anim";
        case Attach::Kind::Spine:
            return "spine";
        case Attach::Kind::Ik2D:
            return "ik2d";
        case Attach::Kind::Ik3D:
            return "ik3d";
        case Attach::Kind::None:
        default:
            return "none";
    }
}

void ParticleEmitter::syncAttach() {
    syncEmitterSources(*config(), *sim(), *attach(), *skinSource());
}

void ParticleEmitter::setSkinSource(animation::AnimSkin *skin, animation::AnimPose *pose) {
    auto s = skinSource();
    s->skin = skin;
    s->pose = pose;
    s->enabled = skin != nullptr && pose != nullptr;
    s->candidatesDirty = true;
    s->lastSkinnedFrame = -1;
}

void ParticleEmitter::setSkinBoneFilter(int skeletonBoneIndex, float minWeight) {
    auto s = skinSource();
    s->filterBone = skeletonBoneIndex;
    s->minWeight = minWeight < 0.f ? 0.f : minWeight;
    s->candidatesDirty = true;
}

void ParticleEmitter::setSkinBoneFilterByName(animation::AnimSkeleton *skeleton,
                                              const std::string &boneName, float minWeight) {
    auto s = skinSource();
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
    auto s = skinSource();
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
    auto s = skinSource();
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
