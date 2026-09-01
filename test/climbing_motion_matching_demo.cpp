#include "PathBesideSource.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"
#include "common/Runtime.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {
int animationIndex(const eve::model3d::ModelData& model, std::string_view name) {
    for (int index = 0; index < model.getAnimationCount(); ++index)
        if (model.getAnimationName(index) == name) return index;
    return -1;
}
}  // namespace

TEST_CASE("climbing.motionMatchingDemoLoadsRedistributableMannequinAndActionClips") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path example    = sourceRoot / "examples" / "climbing-motion-matching";
    const std::filesystem::path assets     = example / "assets" / "kaykit";

    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem != nullptr);
    REQUIRE(filesystem->setIdentity("ev_ut_climbing_motion_matching", true));
    REQUIRE(filesystem->setupWriteDirectory());
    filesystem->allowMountingForPath(assets.string());
    REQUIRE(filesystem->mount(assets.string(), "", false));

    auto* models = eve::model3d::Model3D::create();
    REQUIRE(models != nullptr);
    std::unique_ptr<eve::model3d::ModelData> basic(models->newModelDataFromFile("Rig_Medium_MovementBasic.glb"));
    std::unique_ptr<eve::model3d::ModelData> advanced(models->newModelDataFromFile("Rig_Medium_MovementAdvanced.glb"));
    REQUIRE(basic.get() != nullptr);
    REQUIRE(advanced.get() != nullptr);
    REQUIRE(basic->getMeshCount() >= 6);
    REQUIRE(basic->getAnimationCount() >= 10);
    REQUIRE(advanced->getAnimationCount() >= 10);

    std::unique_ptr<eve::animation::AnimSkeleton> skeleton(
        eve::animation::AnimImporter::loadSkeletonFromModel(basic.get()));
    REQUIRE(skeleton.get() != nullptr);
    CHECK(skeleton->findBone("root") >= 0);
    CHECK(skeleton->findBone("foot.l") >= 0);
    CHECK(skeleton->findBone("foot.r") >= 0);

    for (const auto name : {"Walking_A", "Running_A", "Jump_Full_Short", "Jump_Full_Long"}) {
        const int index = animationIndex(*basic, name);
        REQUIRE(index >= 0);
        std::unique_ptr<eve::animation::AnimClip> clip(
            eve::animation::AnimImporter::loadClipFromModel(basic.get(), skeleton.get(), index));
        REQUIRE(clip.get() != nullptr);
        CHECK(clip->getDuration() > 0.0f);
    }
    for (const auto name : {"Running_Strafe_Left", "Running_Strafe_Right", "Dodge_Forward"})
        CHECK(animationIndex(*advanced, name) >= 0);

    CHECK(std::filesystem::exists(example / "assets" / "kaykit" / "LICENSE-KAYKIT-ANIMATIONS.txt"));
    CHECK(!std::filesystem::exists(example / "assets" / "ue-only" / "GameAnimationSample.uproject"));
    CHECK(filesystem->unmount(assets.string()));
}

TEST_CASE("climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "climbing-motion-matching" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    CHECK(source.str().find("eve.Climbing()") != std::string::npos);
    CHECK(source.str().find("newMotionMatcher") != std::string::npos);
    CHECK(source.str().find("assets/ue-only") == std::string::npos);

    eve::Runtime runtime(4096, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(source.str(), "examples/climbing-motion-matching/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}
