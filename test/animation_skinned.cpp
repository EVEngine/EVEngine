#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace eve::animation;

namespace {

std::string pathBesideThisSource(const char *relative) {
    std::string here = __FILE__;
    auto slash       = here.find_last_of("/\\");
    std::string dir  = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/" + relative;
}

bool fileExists(const std::string &path) {
    return std::filesystem::is_regular_file(path);
}

std::string cesiumManGltf() {
    return pathBesideThisSource("assets/skinned/cesium_man/glTF/CesiumMan.gltf");
}

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManGltf();
    if (fileExists(gltf)) return true;
    std::printf(
        "animation.skinned: missing %s — run scripts/download_skinned_character.sh "
        "(or cmake --build --target download_skinned_character / "
        "-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=ON)\n",
        gltf.c_str());
    return false;
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

float vertexDeltaMax(const std::vector<float> &a, const std::vector<float> &b) {
    const size_t n = std::min(a.size(), b.size());
    float maxd     = 0.f;
    for (size_t i = 0; i < n; ++i) {
        maxd = std::max(maxd, std::fabs(a[i] - b[i]));
    }
    return maxd;
}

}  // namespace

TEST_CASE("animation.skinned.mat4FromTRSIdentity") {
    TransformTRS t = TransformTRS::identity();
    Mat4 m         = Mat4::fromTRS(t);
    CHECK(std::fabs(m.m[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[5] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[10] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[15] - 1.f) < 1e-5f);
    float ox, oy, oz;
    m.transformPoint(1.f, 2.f, 3.f, ox, oy, oz);
    CHECK(std::fabs(ox - 1.f) < 1e-5f);
    CHECK(std::fabs(oy - 2.f) < 1e-5f);
    CHECK(std::fabs(oz - 3.f) < 1e-5f);
}

TEST_CASE("animation.skinned.worldMatrixMatchesTRS") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root = sk->addBone("root", -1);
    const int child = sk->addBone("child", root);
    sk->setBindPosition(child, 0.f, 1.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 10.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    CHECK(std::fabs(pose->getWorldPositionX(child) - 10.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldPositionY(child) - 1.f) < 1e-4f);

    float mat[16];
    pose->getWorldMatrix(child, mat);
    CHECK(std::fabs(mat[12] - 10.f) < 1e-4f);
    CHECK(std::fabs(mat[13] - 1.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldMatrixElement(child, 12) - 10.f) < 1e-4f);
}

TEST_CASE("animation.skinned.cesiumMan.loadSkinAndDeform") {
    if (!ensureSkinnedAssets()) return;

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_animation_skinned", true));

    std::unique_ptr<eve::model3d::Model3D> models(eve::model3d::Model3D::create());
    std::unique_ptr<eve::model3d::ModelData> model(
        models->newModelDataFromFile(cesiumManGltf()));
    REQUIRE(model != nullptr);
    REQUIRE(!model->empty());
    CHECK(model->getMeshCount() >= 1);
    CHECK(model->getAnimationCount() >= 1);

    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    CHECK(model->hasBones(meshIndex));
    CHECK(model->getBoneCount(meshIndex) >= 2);
    CHECK(model->getVertexCount(meshIndex) > 10);

    // Weight bookkeeping: every bone weight references a valid vertex.
    int totalWeights = 0;
    for (int b = 0; b < model->getBoneCount(meshIndex); ++b) {
        const std::string name = model->getBoneName(meshIndex, b);
        CHECK(!name.empty());
        // Inverse-bind should be a finite matrix (translation column often non-zero).
        float sumAbs = 0.f;
        for (int e = 0; e < 16; ++e) {
            const float v = model->getInverseBindMatrixElement(meshIndex, b, e);
            CHECK(std::isfinite(v));
            sumAbs += std::fabs(v);
        }
        CHECK(sumAbs > 0.f);

        const int wc = model->getBoneWeightCount(meshIndex, b);
        totalWeights += wc;
        for (int w = 0; w < wc; ++w) {
            const int vi = model->getBoneWeightVertex(meshIndex, b, w);
            CHECK(vi >= 0);
            CHECK(vi < model->getVertexCount(meshIndex));
            const float wv = model->getBoneWeightValue(meshIndex, b, w);
            CHECK(wv >= 0.f);
            CHECK(wv <= 1.f + 1e-3f);
        }
    }
    CHECK(totalWeights > 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton != nullptr);
    CHECK(skeleton->getBoneCount() >= model->getBoneCount(meshIndex));

    // Skin joints should resolve onto the imported hierarchy by name.
    for (int b = 0; b < model->getBoneCount(meshIndex); ++b) {
        const std::string name = model->getBoneName(meshIndex, b);
        CHECK(skeleton->findBone(name) >= 0);
    }

    std::unique_ptr<AnimClip> clip(
        AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip != nullptr);
    CHECK(clip->getDuration() > 0.1f);

    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin != nullptr);
    CHECK(skin->getVertexCount() == model->getVertexCount(meshIndex));
    CHECK(skin->getBoneCount() >= 2);

    // Spot-check influence renormalization: weights per vertex sum to ~0 or ~1.
    int weightedVerts = 0;
    for (int v = 0; v < skin->getVertexCount(); ++v) {
        float sum = 0.f;
        for (int i = 0; i < skin->getInfluenceCount(); ++i) {
            sum += skin->getVertexWeight(v, i);
        }
        if (sum > 1e-5f) {
            ++weightedVerts;
            CHECK(std::fabs(sum - 1.f) < 1e-3f);
        }
    }
    CHECK(weightedVerts > 0);

    std::unique_ptr<AnimPose> pose0(new AnimPose());
    std::unique_ptr<AnimPose> pose1(new AnimPose());
    clip->sample(0.f, pose0.get(), skeleton.get());
    pose0->computeWorld(skeleton.get());
    const float mid = clip->getDuration() * 0.5f;
    clip->sample(mid, pose1.get(), skeleton.get());
    pose1->computeWorld(skeleton.get());

    std::vector<float> skinned0, skinned1;
    REQUIRE(skin->skinPositionsTo(pose0.get(), skinned0));
    REQUIRE(skin->skinPositionsTo(pose1.get(), skinned1));
    CHECK(skinned0.size() == static_cast<size_t>(skin->getVertexCount()) * 3u);
    CHECK(skinned1.size() == skinned0.size());

    // Walking clip should move skinned vertices between t=0 and mid-clip.
    const float delta = vertexDeltaMax(skinned0, skinned1);
    CHECK(delta > 0.01f);

    // Player path: advance a few frames and keep deforming.
    std::unique_ptr<AnimPlayer> player(new AnimPlayer(skeleton.get()));
    player->play(clip.get());
    for (int i = 0; i < 8; ++i) player->update(1.f / 30.f);
    AnimPose *live = player->getPose();
    REQUIRE(live != nullptr);
    live->computeWorld(skeleton.get());
    std::vector<float> skinnedLive;
    REQUIRE(skin->skinPositionsTo(live, skinnedLive));
    CHECK(vertexDeltaMax(skinned0, skinnedLive) > 1e-4f);
}

TEST_CASE("animation.skinned.cesiumMan.animationFactory") {
    if (!ensureSkinnedAssets()) return;

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_animation_skinned_factory", true));

    std::unique_ptr<eve::model3d::Model3D> models(eve::model3d::Model3D::create());
    std::unique_ptr<eve::model3d::ModelData> model(
        models->newModelDataFromFile(cesiumManGltf()));
    REQUIRE(model != nullptr);

    std::unique_ptr<Animation> anim(Animation::create());
    std::unique_ptr<AnimSkeleton> sk(anim->newSkeletonFromModel(model.get()));
    REQUIRE(sk != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkin> skin(anim->newSkinFromModel(model.get(), meshIndex, sk.get()));
    REQUIRE(skin != nullptr);
    CHECK(skin->getVertexCount() > 0);
    std::unique_ptr<AnimClip> clip(anim->newClipFromModel(model.get(), sk.get(), 0));
    REQUIRE(clip != nullptr);
    CHECK(clip->getDuration() > 0.f);
}
