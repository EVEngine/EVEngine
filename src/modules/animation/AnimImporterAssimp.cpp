#include "animation/AnimImporter.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <assimp/anim.h>
#include <assimp/matrix4x4.h>
#include <assimp/quaternion.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>

#include <vector>

namespace eve::animation {

namespace {

void decomposeBind(const aiMatrix4x4 &m, float &px, float &py, float &pz, float &qx, float &qy,
                   float &qz, float &qw, float &sx, float &sy, float &sz) {
    aiVector3D scaling, position;
    aiQuaternion rotation;
    m.Decompose(scaling, rotation, position);
    px = position.x;
    py = position.y;
    pz = position.z;
    qx = rotation.x;
    qy = rotation.y;
    qz = rotation.z;
    qw = rotation.w;
    sx = scaling.x;
    sy = scaling.y;
    sz = scaling.z;
}

float ticksToSeconds(const aiAnimation *anim, double ticks) {
    double tps = anim->mTicksPerSecond;
    if (tps <= 1e-8) tps = 25.0;
    return static_cast<float>(ticks / tps);
}

}  // namespace

void AnimImporter::collectBones(const aiNode *node, int parent, AnimSkeleton *skeleton,
                                std::vector<const aiNode *> &order) {
    if (!node) return;
    const std::string name = node->mName.C_Str();
    const std::string boneName = name.empty() ? ("node_" + std::to_string(order.size())) : name;
    const int id               = skeleton->addBone(boneName, parent);
    order.push_back(node);

    float px, py, pz, qx, qy, qz, qw, sx, sy, sz;
    decomposeBind(node->mTransformation, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    skeleton->setBindPosition(id, px, py, pz);
    skeleton->setBindRotation(id, qx, qy, qz, qw);
    skeleton->setBindScale(id, sx, sy, sz);

    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        collectBones(node->mChildren[i], id, skeleton, order);
    }
}

AnimSkeleton *AnimImporter::loadSkeleton(const aiScene *scene) {
    if (!scene || !scene->mRootNode) {
        throw Exception("AnimImporter.loadSkeleton: invalid scene");
    }
    auto *skeleton = new AnimSkeleton();
    std::vector<const aiNode *> order;
    collectBones(scene->mRootNode, -1, skeleton, order);
    if (skeleton->getBoneCount() == 0) {
        delete skeleton;
        throw Exception("AnimImporter.loadSkeleton: no bones");
    }
    return skeleton;
}

AnimClip *AnimImporter::loadClip(const aiScene *scene, const AnimSkeleton *skeleton,
                                 int animIndex) {
    if (!scene || !skeleton) {
        throw Exception("AnimImporter.loadClip: null scene/skeleton");
    }
    if (animIndex < 0 || animIndex >= getAnimationCount(scene)) {
        throw Exception("AnimImporter.loadClip: invalid anim index %d", animIndex);
    }
    const aiAnimation *anim = scene->mAnimations[animIndex];
    auto *clip              = new AnimClip(anim->mName.length ? anim->mName.C_Str() : "anim");
    clip->setDuration(ticksToSeconds(anim, anim->mDuration));
    clip->setLoop(true);
    clip->setSampleRate(30.f);

    for (unsigned c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim *channel = anim->mChannels[c];
        if (!channel) continue;
        const int bone = skeleton->findBone(channel->mNodeName.C_Str());
        if (bone < 0) continue;

        for (unsigned i = 0; i < channel->mNumPositionKeys; ++i) {
            const aiVectorKey &k = channel->mPositionKeys[i];
            clip->addPositionKey(bone, ticksToSeconds(anim, k.mTime), k.mValue.x, k.mValue.y,
                                 k.mValue.z);
        }
        for (unsigned i = 0; i < channel->mNumRotationKeys; ++i) {
            const aiQuatKey &k = channel->mRotationKeys[i];
            clip->addRotationKey(bone, ticksToSeconds(anim, k.mTime), k.mValue.x, k.mValue.y,
                                 k.mValue.z, k.mValue.w);
        }
        for (unsigned i = 0; i < channel->mNumScalingKeys; ++i) {
            const aiVectorKey &k = channel->mScalingKeys[i];
            clip->addScaleKey(bone, ticksToSeconds(anim, k.mTime), k.mValue.x, k.mValue.y,
                              k.mValue.z);
        }
    }
    return clip;
}

int AnimImporter::getAnimationCount(const aiScene *scene) {
    if (!scene) return 0;
    return static_cast<int>(scene->mNumAnimations);
}

std::string AnimImporter::getAnimationName(const aiScene *scene, int animIndex) {
    if (!scene || animIndex < 0 || animIndex >= getAnimationCount(scene)) return {};
    const aiAnimation *a = scene->mAnimations[animIndex];
    return a->mName.length ? a->mName.C_Str() : std::string("anim") + std::to_string(animIndex);
}

}  // namespace eve::animation
