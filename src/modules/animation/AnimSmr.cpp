#include "animation/AnimSmr.h"
#include "animation/AnimSmrNeural.h"
#include "common/Capability.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace eve::animation {
namespace {

std::string normalizeToken(const std::string& name) {
    const size_t separator = name.find_last_of(":|/");
    const size_t begin     = separator == std::string::npos ? 0 : separator + 1;
    std::string  out;
    out.reserve(name.size() - begin);
    for (size_t i = begin; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool tokenContains(const std::string& token, const char* needle) { return token.find(needle) != std::string::npos; }

AnimSmrBodyPart classifyBone(const std::string& boneName) {
    const std::string token = normalizeToken(boneName);
    if (tokenContains(token, "head") || tokenContains(token, "neck")) return AnimSmrBodyPart::Head;
    if (tokenContains(token, "arm") || tokenContains(token, "hand") || tokenContains(token, "wrist") ||
        tokenContains(token, "elbow") || tokenContains(token, "shoulder") || tokenContains(token, "clavicle") ||
        tokenContains(token, "finger") || tokenContains(token, "forearm"))
        return AnimSmrBodyPart::Arm;
    if (tokenContains(token, "leg") || tokenContains(token, "foot") || tokenContains(token, "toe") ||
        tokenContains(token, "ankle") || tokenContains(token, "knee") || tokenContains(token, "upleg") ||
        tokenContains(token, "thigh") || tokenContains(token, "calf"))
        return AnimSmrBodyPart::Leg;
    if (tokenContains(token, "spine") || tokenContains(token, "chest") || tokenContains(token, "torso") ||
        tokenContains(token, "hips") || tokenContains(token, "pelvis") || tokenContains(token, "abdomen") ||
        tokenContains(token, "ribcage") || tokenContains(token, "belly") || token == "root" ||
        tokenContains(token, "hip"))
        return AnimSmrBodyPart::Torso;
    return AnimSmrBodyPart::Other;
}

bool partsInteract(AnimSmrBodyPart a, AnimSmrBodyPart b) {
    if (a > b) std::swap(a, b);
    if (a == AnimSmrBodyPart::Arm &&
        (b == AnimSmrBodyPart::Torso || b == AnimSmrBodyPart::Arm || b == AnimSmrBodyPart::Head))
        return true;
    if (a == AnimSmrBodyPart::Leg && b == AnimSmrBodyPart::Leg) return true;
    if (a == AnimSmrBodyPart::Torso && b == AnimSmrBodyPart::Head) return true;
    return false;
}

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3  add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3  sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3  mul(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

Vec3 transformPoint(const TransformTRS& world, float lx, float ly, float lz) {
    const float x = world.qx, y = world.qy, z = world.qz, w = world.qw;
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    const float r00 = (1.f - (yy + zz)) * world.sx;
    const float r10 = (xy + wz) * world.sx;
    const float r20 = (xz - wy) * world.sx;
    const float r01 = (xy - wz) * world.sy;
    const float r11 = (1.f - (xx + zz)) * world.sy;
    const float r21 = (yz + wx) * world.sy;
    const float r02 = (xz + wy) * world.sz;
    const float r12 = (yz - wx) * world.sz;
    const float r22 = (1.f - (xx + yy)) * world.sz;
    return {world.px + r00 * lx + r01 * ly + r02 * lz, world.py + r10 * lx + r11 * ly + r12 * lz,
            world.pz + r20 * lx + r21 * ly + r22 * lz};
}

float skeletonExtent(const AnimSkeleton* skeleton) {
    AnimPose bind;
    skeleton->applyBindPose(&bind);
    bind.computeWorld(skeleton);
    if (skeleton->getBoneCount() == 0) return 1.f;
    const TransformTRS& root = bind.world(0);
    float               maxR = 0.f;
    for (int bone = 1; bone < skeleton->getBoneCount(); ++bone) {
        const TransformTRS& world = bind.world(bone);
        maxR = std::max(maxR, length(sub({world.px, world.py, world.pz}, {root.px, root.py, root.pz})));
    }
    return std::max(maxR, 1e-3f);
}

int findBoneFlexible(const AnimSkeleton* skeleton, const std::string& name) {
    if (!skeleton || name.empty()) return -1;
    const int exact = skeleton->findBone(name);
    if (exact >= 0) return exact;
    const std::string want = normalizeToken(name);
    int               hit  = -1;
    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        if (normalizeToken(skeleton->getBoneName(bone)) != want) continue;
        if (hit >= 0) return -1;
        hit = bone;
    }
    return hit;
}

struct ResolvedChain {
    int root = -1;
    int mid  = -1;
    int tip  = -1;
};

std::vector<ResolvedChain> resolveChains(const AnimSkeleton*                skeleton,
                                         const std::vector<AnimSmrIkChain>& configuredChains) {
    std::vector<ResolvedChain> out;
    for (const AnimSmrIkChain& chain : configuredChains) {
        ResolvedChain resolved{findBoneFlexible(skeleton, chain.rootBone), findBoneFlexible(skeleton, chain.midBone),
                               findBoneFlexible(skeleton, chain.tipBone)};
        if (resolved.root >= 0 && resolved.mid >= 0 && resolved.tip >= 0) out.push_back(resolved);
    }
    if (!out.empty()) return out;

    auto tryPush = [&](const char* root, const char* mid, const char* tip) {
        ResolvedChain resolved{findBoneFlexible(skeleton, root), findBoneFlexible(skeleton, mid),
                               findBoneFlexible(skeleton, tip)};
        if (resolved.root >= 0 && resolved.mid >= 0 && resolved.tip >= 0) out.push_back(resolved);
    };
    tryPush("LeftShoulder", "LeftElbow", "LeftHand");
    tryPush("RightShoulder", "RightElbow", "RightHand");
    tryPush("LeftUpLeg", "LeftLeg", "LeftFoot");
    tryPush("RightUpLeg", "RightLeg", "RightFoot");
    tryPush("leftarm", "leftforearm", "lefthand");
    tryPush("rightarm", "rightforearm", "righthand");
    tryPush("leftupleg", "leftleg", "leftfoot");
    tryPush("rightupleg", "rightleg", "rightfoot");
    tryPush("shoulder_l", "elbow_l", "hand_l");
    tryPush("shoulder_r", "elbow_r", "hand_r");
    tryPush("thigh_l", "calf_l", "foot_l");
    tryPush("thigh_r", "calf_r", "foot_r");
    return out;
}

int chainForTip(const std::vector<ResolvedChain>& chains, int tipBone) {
    for (int i = 0; i < static_cast<int>(chains.size()); ++i) {
        if (chains[static_cast<size_t>(i)].tip == tipBone) return i;
    }
    return -1;
}

struct InteractionPair {
    int  observer = -1;
    int  subject  = -1;
    Vec3 relative{};
};

void collectInteractions(const AnimSmrSensorCloud& cloud, const std::vector<float>& xyz, float contactDistance,
                         std::vector<InteractionPair>& out) {
    out.clear();
    const int count = cloud.getSensorCount();
    for (int i = 0; i < count; ++i) {
        const AnimSmrBodyPart partI = cloud.getSensorPart(i);
        for (int j = i + 1; j < count; ++j) {
            const AnimSmrBodyPart partJ = cloud.getSensorPart(j);
            if (!partsInteract(partI, partJ)) continue;
            int observer = i;
            int subject  = j;
            if (partJ == AnimSmrBodyPart::Torso && partI != AnimSmrBodyPart::Torso) {
                observer = j;
                subject  = i;
            } else if (partI == AnimSmrBodyPart::Torso && partJ != AnimSmrBodyPart::Torso) {
                observer = i;
                subject  = j;
            }
            const Vec3 po{xyz[static_cast<size_t>(observer) * 3], xyz[static_cast<size_t>(observer) * 3 + 1],
                          xyz[static_cast<size_t>(observer) * 3 + 2]};
            const Vec3 pj{xyz[static_cast<size_t>(subject) * 3], xyz[static_cast<size_t>(subject) * 3 + 1],
                          xyz[static_cast<size_t>(subject) * 3 + 2]};
            const Vec3 delta = sub(pj, po);
            if (length(delta) > contactDistance) continue;
            out.push_back({observer, subject, delta});
        }
    }
}

int matchSensor(const AnimSmrSensorCloud& sourceCloud, int sourceIndex, const AnimSmrSensorCloud& targetCloud) {
    const AnimSmrSensor& source = sourceCloud.getSensor(sourceIndex);
    int                  best   = -1;
    for (int i = 0; i < targetCloud.getSensorCount(); ++i) {
        const AnimSmrSensor& target = targetCloud.getSensor(i);
        if (target.part != source.part) continue;
        if (target.semanticKey == source.semanticKey) return i;
        const size_t sourceHash = source.semanticKey.find('#');
        const size_t targetHash = target.semanticKey.find('#');
        if (sourceHash != std::string::npos && targetHash != std::string::npos &&
            source.semanticKey.substr(sourceHash) == target.semanticKey.substr(targetHash)) {
            if (best < 0) best = i;
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < targetCloud.getSensorCount(); ++i) {
        if (targetCloud.getSensorPart(i) == source.part) return i;
    }
    return -1;
}

void writeRotationKey(AnimClip& clip, int boneIndex, int keyIndex, float time, const TransformTRS& local) {
    if (keyIndex >= 0 && keyIndex < clip.getRotationKeyCount(boneIndex)) {
        clip.setRotationKey(boneIndex, keyIndex, time, local.qx, local.qy, local.qz, local.qw);
        return;
    }
    clip.addRotationKey(boneIndex, time, local.qx, local.qy, local.qz, local.qw);
}

}  // namespace


AnimSmrSensorCloud AnimSmrSensorCloud::fromSkeletonDense(const AnimSkeleton* skeleton, int ringsPerBone,
                                                         int pointsPerRing) {
    if (!skeleton) throw Exception("AnimSmrSensorCloud.fromSkeletonDense: skeleton is null");
    if (ringsPerBone < 1 || pointsPerRing < 3)
        throw Exception("AnimSmrSensorCloud.fromSkeletonDense: ringsPerBone>=1 and pointsPerRing>=3 required");

    AnimSmrSensorCloud cloud = fromSkeleton(skeleton);
    std::vector<int>   firstChild(static_cast<size_t>(skeleton->getBoneCount()), -1);
    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        const int parent = skeleton->getParent(bone);
        if (parent >= 0 && firstChild[static_cast<size_t>(parent)] < 0) firstChild[static_cast<size_t>(parent)] = bone;
    }

    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        const int child = firstChild[static_cast<size_t>(bone)];
        if (child < 0) continue;
        const TransformTRS& childBind = skeleton->bindLocal(child);
        const float         len =
            std::sqrt(childBind.px * childBind.px + childBind.py * childBind.py + childBind.pz * childBind.pz);
        if (len < 1e-5f) continue;
        const float radius = len * 0.08f;
        // Build a stable orthonormal frame around the bone axis.
        float ax = childBind.px / len, ay = childBind.py / len, az = childBind.pz / len;
        float bx = 0.f, by = 1.f, bz = 0.f;
        if (std::fabs(ay) > 0.9f) {
            bx = 1.f;
            by = 0.f;
            bz = 0.f;
        }
        float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        float cl = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (cl < 1e-5f) continue;
        cx /= cl;
        cy /= cl;
        cz /= cl;
        float                 dx = ay * cz - az * cy, dy = az * cx - ax * cz, dz = ax * cy - ay * cx;
        const std::string     token = normalizeToken(skeleton->getBoneName(bone));
        const AnimSmrBodyPart part  = classifyBone(skeleton->getBoneName(bone));
        for (int ring = 0; ring < ringsPerBone; ++ring) {
            const float t = (static_cast<float>(ring) + 1.f) / (static_cast<float>(ringsPerBone) + 1.f);
            for (int sample = 0; sample < pointsPerRing; ++sample) {
                const float   angle = 6.28318530718f * static_cast<float>(sample) / static_cast<float>(pointsPerRing);
                const float   ca = std::cos(angle), sa = std::sin(angle);
                AnimSmrSensor sensor;
                sensor.boneIndex   = bone;
                sensor.part        = part;
                sensor.localX      = childBind.px * t + (cx * ca + dx * sa) * radius;
                sensor.localY      = childBind.py * t + (cy * ca + dy * sa) * radius;
                sensor.localZ      = childBind.pz * t + (cz * ca + dz * sa) * radius;
                sensor.semanticKey = token + "#r" + std::to_string(ring) + "p" + std::to_string(sample);
                cloud.sensors_.push_back(sensor);
            }
        }
    }
    return cloud;
}

AnimSmrSensorCloud AnimSmrSensorCloud::fromSkeleton(const AnimSkeleton* skeleton) {
    if (!skeleton) throw Exception("AnimSmrSensorCloud.fromSkeleton: skeleton is null");

    AnimSmrSensorCloud cloud;
    std::vector<int>   firstChild(static_cast<size_t>(skeleton->getBoneCount()), -1);
    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        const int parent = skeleton->getParent(bone);
        if (parent >= 0 && firstChild[static_cast<size_t>(parent)] < 0) firstChild[static_cast<size_t>(parent)] = bone;
    }

    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        const std::string     token = normalizeToken(skeleton->getBoneName(bone));
        const AnimSmrBodyPart part  = classifyBone(skeleton->getBoneName(bone));
        AnimSmrSensor         joint;
        joint.boneIndex   = bone;
        joint.part        = part;
        joint.semanticKey = token + "#0";
        cloud.sensors_.push_back(joint);

        const int child = firstChild[static_cast<size_t>(bone)];
        if (child < 0) continue;
        const TransformTRS& childBind = skeleton->bindLocal(child);
        AnimSmrSensor       mid;
        mid.boneIndex   = bone;
        mid.part        = part;
        mid.localX      = childBind.px * 0.5f;
        mid.localY      = childBind.py * 0.5f;
        mid.localZ      = childBind.pz * 0.5f;
        mid.semanticKey = token + "#1";
        cloud.sensors_.push_back(mid);
    }
    return cloud;
}

const AnimSmrSensor& AnimSmrSensorCloud::getSensor(int index) const {
    if (index < 0 || index >= getSensorCount()) throw Exception("AnimSmrSensorCloud.getSensor: index out of range");
    return sensors_[static_cast<size_t>(index)];
}

AnimSmrBodyPart AnimSmrSensorCloud::getSensorPart(int index) const { return getSensor(index).part; }

void AnimSmrSensorCloud::evaluateWorldPositions(const AnimPose* pose, std::vector<float>& outXYZ) const {
    if (!pose) throw Exception("AnimSmrSensorCloud.evaluateWorldPositions: pose is null");
    outXYZ.resize(static_cast<size_t>(getSensorCount()) * 3);
    for (int i = 0; i < getSensorCount(); ++i) {
        const AnimSmrSensor& sensor = sensors_[static_cast<size_t>(i)];
        if (sensor.boneIndex < 0 || sensor.boneIndex >= pose->getBoneCount()) {
            outXYZ[static_cast<size_t>(i) * 3]     = 0.f;
            outXYZ[static_cast<size_t>(i) * 3 + 1] = 0.f;
            outXYZ[static_cast<size_t>(i) * 3 + 2] = 0.f;
            continue;
        }
        const Vec3 world = transformPoint(pose->world(sensor.boneIndex), sensor.localX, sensor.localY, sensor.localZ);
        outXYZ[static_cast<size_t>(i) * 3]     = world.x;
        outXYZ[static_cast<size_t>(i) * 3 + 1] = world.y;
        outXYZ[static_cast<size_t>(i) * 3 + 2] = world.z;
    }
}

int smrRefineRetargetedClip(const AnimClip& sourceClip, AnimClip& targetClip, const AnimSkeleton* sourceSkeleton,
                            const AnimSkeleton* targetSkeleton, AnimRetargetProfile& profile) {
    if (!sourceSkeleton || !targetSkeleton) throw Exception("smrRefineRetargetedClip: skeleton is null");

    if (!profile.skinnedInteractionPreserve_) {
        profile.interactionCorrectionCount_ = 0;
        profile.neuralInferenceCount_       = 0;
        return 0;
    }

    profile.neuralInferenceCount_ = 0;
    if (profile.neuralRetargetEnabled_) {
        if (auto* neural = cap::query<ISmrNeuralRetarget>()) {
            if (neural->isReady(&profile)) {
                SmrNeuralRequest request;
                request.sourceClip     = &sourceClip;
                request.targetClip     = &targetClip;
                request.sourceSkeleton = sourceSkeleton;
                request.targetSkeleton = targetSkeleton;
                request.profile        = &profile;
                auto neuralResult      = neural->retarget(request);
                if (neuralResult.ok()) {
                    profile.interactionCorrectionCount_ = neuralResult.value().framesWritten;
                    profile.neuralInferenceCount_       = std::max(1, neuralResult.value().framesWritten);
                    return profile.interactionCorrectionCount_;
                }
            }
        }
    }

    const AnimSmrSensorCloud         sourceCloud = AnimSmrSensorCloud::fromSkeleton(sourceSkeleton);
    const AnimSmrSensorCloud         targetCloud = AnimSmrSensorCloud::fromSkeleton(targetSkeleton);
    const std::vector<ResolvedChain> chains      = resolveChains(targetSkeleton, profile.interactionIkChains_);
    if (chains.empty() || sourceCloud.getSensorCount() == 0 || targetCloud.getSensorCount() == 0) {
        profile.interactionCorrectionCount_ = 0;
        return 0;
    }

    const float sourceExtent = skeletonExtent(sourceSkeleton);
    const float targetExtent = skeletonExtent(targetSkeleton);
    const float sizeScale    = targetExtent / sourceExtent;
    const float contactDistance =
        std::max(1e-4f, profile.interactionContactThreshold_ > 0.f ? profile.interactionContactThreshold_
                                                                   : sourceExtent * 0.08f);
    const float weight = clampf(profile.interactionCorrectionWeight_, 0.f, 1.f);

    const float duration   = targetClip.getDuration();
    const float sampleRate = std::max(targetClip.getSampleRate(), 1.f);
    const int   frameCount = duration <= 0.f ? 1 : std::max(1, static_cast<int>(std::ceil(duration * sampleRate)));

    AnimPose                     sourcePose;
    AnimPose                     targetPose;
    std::vector<float>           sourceXYZ;
    std::vector<float>           targetXYZ;
    std::vector<InteractionPair> pairs;
    int                          corrections = 0;

    for (int frame = 0; frame <= frameCount; ++frame) {
        if (duration <= 0.f && frame > 0) break;
        const float time =
            duration <= 0.f ? 0.f : duration * static_cast<float>(frame) / static_cast<float>(frameCount);
        sourceClip.sample(time, &sourcePose, sourceSkeleton);
        sourcePose.computeWorld(sourceSkeleton);
        targetClip.sample(time, &targetPose, targetSkeleton);
        targetPose.computeWorld(targetSkeleton);

        sourceCloud.evaluateWorldPositions(&sourcePose, sourceXYZ);
        targetCloud.evaluateWorldPositions(&targetPose, targetXYZ);
        collectInteractions(sourceCloud, sourceXYZ, contactDistance, pairs);

        bool frameChanged = false;
        for (const InteractionPair& pair : pairs) {
            const int targetObserver = matchSensor(sourceCloud, pair.observer, targetCloud);
            const int targetSubject  = matchSensor(sourceCloud, pair.subject, targetCloud);
            if (targetObserver < 0 || targetSubject < 0) continue;

            const AnimSmrSensor& subjectSensor = targetCloud.getSensor(targetSubject);
            const int            chainIndex    = chainForTip(chains, subjectSensor.boneIndex);
            if (chainIndex < 0) continue;

            const Vec3 observerWorld{targetXYZ[static_cast<size_t>(targetObserver) * 3],
                                     targetXYZ[static_cast<size_t>(targetObserver) * 3 + 1],
                                     targetXYZ[static_cast<size_t>(targetObserver) * 3 + 2]};
            const Vec3 desiredSubject = add(observerWorld, mul(pair.relative, sizeScale));

            const TransformTRS& tipWorld = targetPose.world(subjectSensor.boneIndex);
            const Vec3          tipPos{tipWorld.px, tipWorld.py, tipWorld.pz};
            const Vec3          sensorWorld{targetXYZ[static_cast<size_t>(targetSubject) * 3],
                                   targetXYZ[static_cast<size_t>(targetSubject) * 3 + 1],
                                   targetXYZ[static_cast<size_t>(targetSubject) * 3 + 2]};
            const Vec3          tipTarget = sub(desiredSubject, sub(sensorWorld, tipPos));

            const ResolvedChain& chain = chains[static_cast<size_t>(chainIndex)];
            if (targetPose.solveTwoBoneIK(targetSkeleton, chain.root, chain.mid, chain.tip, tipTarget.x, tipTarget.y,
                                          tipTarget.z, weight)) {
                writeRotationKey(targetClip, chain.root, frame, time, targetPose.local(chain.root));
                writeRotationKey(targetClip, chain.mid, frame, time, targetPose.local(chain.mid));
                writeRotationKey(targetClip, chain.tip, frame, time, targetPose.local(chain.tip));
                targetPose.computeWorld(targetSkeleton);
                targetCloud.evaluateWorldPositions(&targetPose, targetXYZ);
                frameChanged = true;
            }
        }
        if (frameChanged) ++corrections;
    }

    profile.interactionCorrectionCount_ = corrections;
    return corrections;
}

}  // namespace eve::animation
