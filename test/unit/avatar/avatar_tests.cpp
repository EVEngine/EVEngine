// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "avatar.cpp",
    "avatar_vrm.cpp",
    "avatar_appearance.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar)
#include "PrivateTestScope.h"
#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "inventory/Equipment.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

// avatar.cpp
namespace avatar_contract {
using namespace eve::avatar;

class RecordingLive2D final : public ILive2DBackend {
public:
    std::string getName() const override { return "recording"; }
    bool        loadModel(const std::string& path) override {
        loaded = path;
        return !path.empty();
    }
    void  update(float) override { ++updates; }
    void  setParameter(const std::string& name, float value) override { parameters[name] = value; }
    float getParameter(const std::string& name) const override {
        auto it = parameters.find(name);
        return it == parameters.end() ? 0.f : it->second;
    }
    bool setExpression(const std::string& name) override {
        expression = name;
        return true;
    }
    bool setMotion(const std::string& name) override {
        motion = name;
        return true;
    }
    std::string                            loaded;
    std::string                            expression;
    std::string                            motion;
    int                                    updates = 0;
    std::unordered_map<std::string, float> parameters;
};

ILive2DBackend* makeRecording() { return new RecordingLive2D(); }

void dispose(AvatarInstance* instance) {
    instance->release();
    delete instance;
}

TEST_CASE("avatar.image.layersExpressionsAndTransformProjectToRenderables") {
    auto*     module = Avatar::create();
    const int before = module->getAvatarCount();
    auto*     avatar = module->newImageAvatar();
    REQUIRE(avatar != nullptr);
    CHECK_EQ(avatar->getKind(), std::string("image"));
    CHECK_EQ(module->getAvatarCount(), before + 1);
    REQUIRE(avatar->addLayer("body", nullptr, 0));
    REQUIRE(avatar->addLayer("face", nullptr, 1));
    CHECK(!avatar->addLayer("body", nullptr, 2));
    CHECK_EQ(avatar->getLayerCount(), 2);
    CHECK_EQ(avatar->getLayerName(0), std::string("body"));
    CHECK(avatar->getLayerRenderable("missing") == nullptr);
    CHECK(!avatar->setLayerVisible("missing", true));
    REQUIRE(avatar->setLayerVisible("face", false));
    REQUIRE(avatar->setLayerSize("body", 200.f, 400.f));
    REQUIRE(avatar->setLayerOffset("face", 10.f, -20.f));
    REQUIRE(avatar->defineExpression("smile", "face=1"));
    REQUIRE(avatar->applyExpression("smile"));
    CHECK_EQ(avatar->getExpression(), std::string("smile"));
    avatar->setPosition(100.f, 200.f);
    avatar->setScale(1.5f, 2.f);
    avatar->setVisible(true);
    avatar->sync();
    CHECK_EQ(avatar->getX(), 100.f);
    CHECK_EQ(avatar->getY(), 200.f);
    CHECK_EQ(avatar->getScaleX(), 1.5f);
    CHECK_EQ(avatar->getScaleY(), 2.f);
    CHECK(avatar->getLayerRenderable("face") != nullptr);
    dispose(avatar);
    CHECK_EQ(module->getAvatarCount(), before);
}

TEST_CASE("avatar.metadata.acceptsValidParametersAndSortsExpressionNames") {
    auto* avatar = Avatar::create()->newImageAvatar();
    REQUIRE(avatar->defineParameter("mouth", .25f, -1.f, 1.f));
    CHECK_EQ(avatar->getParameter("mouth"), .25f);
    CHECK_EQ(avatar->getParameterMinimum("mouth"), -1.f);
    CHECK_EQ(avatar->getParameterMaximum("mouth"), 1.f);
    CHECK(!avatar->defineParameter("", 0.f, 0.f, 1.f));
    CHECK(!avatar->defineParameter("bad", 0.f, 2.f, 1.f));
    REQUIRE(avatar->defineExpression("smile", "mouth=1"));
    REQUIRE(avatar->defineExpression("neutral", "mouth=0"));
    CHECK_EQ(avatar->getExpressionName(0), std::string("neutral"));
    CHECK_EQ(avatar->getExpressionName(1), std::string("smile"));
    REQUIRE(avatar->removeExpression("neutral"));
    CHECK_EQ(avatar->getExpressionCount(), 1);
    dispose(avatar);
}

TEST_CASE("avatar.live2d.nullAndRegisteredBackendsHaveObservableBehavior") {
    Avatar::registerLive2DBackend(nullptr);
    auto* module = Avatar::create();
    CHECK(!Avatar::hasLive2DBackend());
    CHECK_EQ(Avatar::getLive2DBackendName(), std::string("null"));
    auto* fallback = module->newLive2DAvatar();
    CHECK(!fallback->hasLive2DBackend());
    CHECK(fallback->loadLive2DModel("models/portrait"));
    fallback->setParameter("mouth", .4f);
    CHECK_EQ(fallback->getParameter("mouth"), .4f);
    dispose(fallback);

    Avatar::registerLive2DBackend(&makeRecording);
    auto* plugged = module->newLive2DAvatar();
    CHECK(Avatar::hasLive2DBackend());
    CHECK_EQ(plugged->getLive2DBackendName(), std::string("recording"));
    CHECK(!plugged->loadLive2DModel(""));
    REQUIRE(plugged->loadLive2DModel("models/portrait"));
    plugged->setParameter("mouth", .5f);
    plugged->setExpression("smile");
    plugged->setMotion("idle");
    plugged->update(.016f);
    CHECK_EQ(plugged->getParameter("mouth"), .5f);
    dispose(plugged);
    Avatar::registerLive2DBackend(nullptr);
}

TEST_CASE("avatar.equipment.imageProjectionTracksAuthoritativeSlot") {
    auto*                        avatar = Avatar::create()->newImageAvatar();
    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("body");
    CHECK(avatar->defineEquipmentVisual2D("armor.red", "body", "armor", nullptr, 5) == EquipmentVisualChange::Applied);
    CHECK(avatar->bindEquipment(&equipment) == EquipmentVisualChange::Applied);
    CHECK_EQ(avatar->getEquipmentVisualItem("body"), std::string{});
    auto* stack = equipment.stackAt("body");
    REQUIRE(stack != nullptr);
    stack->itemId   = "armor.red";
    stack->quantity = 1;
    avatar->sync();
    CHECK_EQ(avatar->getEquipmentVisualItem("body"), std::string("armor.red"));
    CHECK(avatar->getLayerRenderable("armor")->getVisible());
    stack->clear();
    avatar->sync();
    CHECK_EQ(avatar->getEquipmentVisualItem("body"), std::string{});
    CHECK(!avatar->getLayerRenderable("armor")->getVisible());
    dispose(avatar);
}

TEST_CASE("avatar.equipment.layerOcclusionChangesVisibleRenderStack") {
    auto*                        avatar = Avatar::create()->newImageAvatar();
    eve::inventory::EquipmentSet equipment;
    equipment.defineSlot("shirt");
    equipment.defineSlot("outerwear");
    REQUIRE(avatar->defineEquipmentLayer("shirt", 0) == EquipmentVisualChange::Applied);
    REQUIRE(avatar->defineEquipmentLayer("outerwear", 1) == EquipmentVisualChange::Applied);
    REQUIRE(avatar->defineEquipmentVisual2D("linen", "shirt", "shirt", nullptr, 0) == EquipmentVisualChange::Applied);
    REQUIRE(avatar->defineEquipmentVisual2D("coat", "outerwear", "coat", nullptr, 0) == EquipmentVisualChange::Applied);
    equipment.stackAt("shirt")->itemId       = "linen";
    equipment.stackAt("shirt")->quantity     = 1;
    equipment.stackAt("outerwear")->itemId   = "coat";
    equipment.stackAt("outerwear")->quantity = 1;
    REQUIRE(avatar->bindEquipment(&equipment) == EquipmentVisualChange::Applied);
    CHECK_EQ(avatar->getEquipmentRenderStackCount(), 2);
    REQUIRE(avatar->addEquipmentLayerOcclusion("outerwear", "shirt") == EquipmentVisualChange::Applied);
    CHECK_EQ(avatar->getEquipmentRenderStackCount(), 1);
    CHECK(!avatar->getLayerRenderable("shirt")->getVisible());
    equipment.stackAt("outerwear")->clear();
    avatar->sync();
    CHECK_EQ(avatar->getEquipmentRenderStackCount(), 1);
    CHECK(avatar->getLayerRenderable("shirt")->getVisible());
    dispose(avatar);
}

TEST_CASE("avatar.vroid.morphAndMotionAdvanceOwnedProjection") {
    auto* avatar = Avatar::create()->newVroidAvatar();
    CHECK_EQ(avatar->getKind(), std::string("vroid"));
    CHECK(!avatar->loadVroidModelPath("missing.vrm"));
    CHECK(avatar->getVroidModelPath().empty());
    eve::graphics::Mesh mesh;
    const float         base[]  = {0.f, 0.f, 0.f};
    const float         delta[] = {0.f, 1.f, 0.f};
    mesh.initMorphBase(1, base);
    REQUIRE(mesh.addMorphTarget("Joy", delta));
    avatar->setMesh(&mesh);
    REQUIRE(avatar->defineExpression("happy", "Joy=1"));
    REQUIRE(avatar->applyExpression("happy"));
    CHECK_EQ(mesh.getMorphWeight("Joy"), 1.f);
    avatar->setParameter("Joy", .5f);
    CHECK_EQ(mesh.getMorphWeight("Joy"), .5f);

    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("hips", -1);
    eve::animation::AnimClip     walk("walk");
    walk.setDuration(1.f);
    walk.setLoop(true);
    walk.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    walk.addPositionKey(root, 1.f, 1.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(avatar->bindAnimPlayer(&player));
    REQUIRE(avatar->registerMotion("walk", &walk));
    avatar->setMotionBlendTime(0.f);
    avatar->setApplyRootMotion(true);
    avatar->setMotion("walk");
    CHECK(player.isPlaying());
    avatar->update(.25f);
    avatar->update(.25f);
    CHECK(std::fabs(avatar->getRootMotionDeltaX() - .25f) < 1e-4f);
    dispose(avatar);
}

}  // namespace avatar_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar_vrm)
#include "PrivateTestScope.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmDocument.h"
#include "zeroerr/unittest.h"

#include <cstring>
#include <string>
#include <vector>

// avatar_vrm.cpp
namespace avatar_vrm_contract {
std::vector<std::byte> glb(std::string json) {
    while (json.size() % 4) json += ' ';
    std::vector<std::byte> bytes(20 + json.size());
    const std::uint32_t    header[] = {0x46546c67, 2, static_cast<std::uint32_t>(bytes.size()),
                                       static_cast<std::uint32_t>(json.size()), 0x4e4f534a};
    std::memcpy(bytes.data(), header, 20);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    return bytes;
}

const std::string minimal = R"({"asset":{"version":"2.0"},"nodes":[{"name":"head"}],
"extensions":{"VRMC_vrm":{"specVersion":"1.0","humanoid":{"humanBones":{"head":{"node":0}}}}}})";

TEST_CASE("avatar.vrm.parsesVersionAndHumanoidReference") {
    auto parsed = eve::avatar::parseVrm(glb(minimal));
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().version, std::string("1.0"));
    CHECK_EQ(parsed.value().humanoid.at("head"), 0);
}

TEST_CASE("avatar.vrm.rejectsTruncationUnknownVersionAndInvalidBone") {
    auto truncated = glb(minimal);
    truncated.pop_back();
    CHECK(!eve::avatar::parseVrm(truncated).ok());
    auto future = minimal;
    future.replace(future.find("specVersion\":\"1.0") + 14, 3, "9.0");
    CHECK(!eve::avatar::parseVrm(glb(future)).ok());
    auto invalid = minimal;
    invalid.replace(invalid.find("\"node\":0"), 8, "\"node\":7");
    REQUIRE(!eve::avatar::parseVrm(glb(invalid)).ok());
}

TEST_CASE("avatar.vrm.failedImportDoesNotPublishPartialState") {
    eve::avatar::AvatarInstance avatar("vroid");
    CHECK(!avatar.loadVroidModel("does-not-exist.vrm").ok());
    CHECK(avatar.getVroidModelPath().empty());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
}
}  // namespace avatar_vrm_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar_appearance)
#include "avatar/Appearance.h"
#include "graphics/Mesh.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// avatar_appearance.cpp
namespace avatar_appearance_contract {
eve::Value json(std::string_view source) {
    auto parsed = eve::Value::fromJson(source);
    if (!parsed.ok()) throw std::runtime_error("invalid test JSON");
    return std::move(parsed).takeValue();
}

eve::Value definition() {
    return json(R"({"schema":"eve.appearance.definition","version":1,"profile":"human.v1",
      "parameters":[{"id":"jaw","min":-1,"max":1,"default":0},{"id":"smile","min":0,"max":1,"default":0}],
      "drivers":[
        {"output":"jawWide","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":1}]},
        {"output":"jawNarrow","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":-1}]},
        {"output":"cheekFix","scale":0.5,"inputs":[{"parameter":"jaw","start":0,"end":1},{"parameter":"smile","start":0,"end":1}]}],
      "slots":[{"id":"hair","default":"long"},{"id":"hat","default":""}],
      "items":[
        {"id":"long","slot":"hair","parts":["longHair"],"hides":[],"excludes":["helmet"]},
        {"id":"short","slot":"hair","parts":["shortHair"],"hides":[],"excludes":[]},
        {"id":"helmet","slot":"hat","parts":["helmet"],"hides":["shortHair"],"excludes":[]}]})");
}

std::string encoded(const eve::Value& value) {
    auto result = value.toJson();
    if (!result.ok()) throw std::runtime_error("JSON encode failed");
    return std::move(result).takeValue();
}

double channel(const eve::avatar::Appearance& appearance, const char* name) {
    return appearance.evaluate().find("channels")->find(name)->asDouble();
}

TEST_CASE("avatar.appearance.signedParametersDriveCorrectiveChannels") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.8,"smile":0.5}})")).ok());
    CHECK(std::abs(channel(appearance, "jawWide") - .8) < 1e-12);
    CHECK_EQ(channel(appearance, "jawNarrow"), 0.0);
    CHECK(std::abs(channel(appearance, "cheekFix") - .2) < 1e-12);
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":-0.6}})")).ok());
    CHECK_EQ(channel(appearance, "jawWide"), 0.0);
    CHECK(std::abs(channel(appearance, "jawNarrow") - .6) < 1e-12);
}

TEST_CASE("avatar.appearance.invalidPatchPreservesStateAndHistory") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    for (const char* patch :
         {R"({"parameters":{"jaw":2}})", R"({"parameters":{"unknown":0}})", R"({"selections":{"hat":"long"}})",
          R"({"unknown":true})", R"({"parameters":{"jaw":true}})"}) {
        CHECK(!appearance.apply(json(patch)).ok());
        CHECK_EQ(encoded(appearance.snapshot()), initial);
        CHECK(!appearance.canUndo());
    }
    auto nonfinite = json(R"({"parameters":{"jaw":0}})");
    nonfinite.find("parameters")->set("jaw", std::numeric_limits<double>::quiet_NaN());
    CHECK(!appearance.apply(nonfinite).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
}

TEST_CASE("avatar.appearance.wardrobeUndoRedoAndProfileRestore") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    CHECK(!appearance.apply(json(R"({"selections":{"hat":"helmet"}})")).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.apply(json(R"({"selections":{"hair":"short","hat":"helmet"}})")).ok());
    const auto equipped = appearance.snapshot();
    const auto visible  = encoded(*appearance.evaluate().find("visibleParts"));
    CHECK(visible.find("helmet") != std::string::npos);
    CHECK(visible.find("shortHair") == std::string::npos);
    REQUIRE(appearance.undo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.redo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), encoded(equipped));
    eve::avatar::Appearance restored;
    REQUIRE(restored.configure(definition()).ok());
    REQUIRE(restored.restore(equipped).ok());
    CHECK_EQ(encoded(restored.evaluate()), encoded(appearance.evaluate()));
    auto foreign = equipped;
    foreign.set("profile", "other.v1");
    CHECK(!restored.restore(foreign).ok());
    CHECK_EQ(encoded(restored.snapshot()), encoded(equipped));
}

TEST_CASE("avatar.appearance.morphProjectionValidatesBeforeWriting") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.75,"smile":0.5}})")).ok());
    eve::graphics::Mesh mesh;
    const float         positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float         delta[]     = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.initMorphBase(3, positions);
    REQUIRE(mesh.addMorphTarget("jawWide", delta));
    REQUIRE(mesh.addMorphTarget("jawNarrow", delta));
    REQUIRE(mesh.setMorphWeight("jawWide", .1f));
    CHECK(!appearance.applyMorphWeights(mesh, "").ok());
    CHECK_EQ(mesh.getMorphWeight("jawWide"), .1f);
    REQUIRE(mesh.addMorphTarget("cheekFix", delta));
    REQUIRE(appearance.applyMorphWeights(mesh, "").ok());
    CHECK_EQ(mesh.getMorphWeight("jawWide"), .75f);
    CHECK_EQ(mesh.getMorphWeight("cheekFix"), .1875f);
}

}  // namespace avatar_appearance_contract
#endif
