#include "avatar/Appearance.h"
#include "common/Module.h"
#include "graphics/Mesh.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

namespace {
eve::Value json(std::string_view source) {
    auto result = eve::Value::fromJson(source);
    if (!result.ok()) throw std::runtime_error("Invalid test JSON");
    return std::move(result).takeValue();
}
eve::Value definition() {
    return json(R"({"schema":"eve.appearance.definition","version":1,"profile":"human.v1",
      "parameters":[{"id":"jaw","min":-1,"max":1,"default":0},{"id":"smile","min":0,"max":1,"default":0}],
      "drivers":[
        {"output":"jawWide","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":1}]},
        {"output":"jawNarrow","scale":1,"inputs":[{"parameter":"jaw","start":0,"end":-1}]},
        {"output":"cheekFix","scale":0.5,"inputs":[{"parameter":"jaw","start":0,"end":1},{"parameter":"smile","start":0,"end":1}]}],
      "slots":[{"id":"hair","default":"long"},{"id":"hat","default":""},{"id":"body","default":"skin"}],
      "items":[
        {"id":"long","slot":"hair","parts":["longHair"],"hides":[],"excludes":["helmet"]},
        {"id":"short","slot":"hair","parts":["shortHair"],"hides":[],"excludes":[]},
        {"id":"helmet","slot":"hat","parts":["helmet"],"hides":["shortHair"],"excludes":[]},
        {"id":"skin","slot":"body","parts":["head","torso"],"hides":[],"excludes":[]}]})");
}
double channel(const eve::avatar::Appearance& appearance, const std::string& name) {
    const auto output = appearance.evaluate();
    return output.find("channels")->find(name)->asDouble();
}
std::string encoded(const eve::Value& value) {
    auto result = value.toJson();
    if (!result.ok()) throw std::runtime_error("JSON encode failed");
    return std::move(result).takeValue();
}
}  // namespace

TEST_CASE("avatar.appearance.signedChannelsAndCorrective") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.8,"smile":0.5}})")).ok());
    CHECK(std::abs(channel(appearance, "cheekFix") - 0.2) < 1e-12);
    CHECK(std::abs(channel(appearance, "jawWide") - 0.8) < 1e-12);
    CHECK_EQ(channel(appearance, "jawNarrow"), 0.0);
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":-0.6}})")).ok());
    CHECK(std::abs(channel(appearance, "jawNarrow") - 0.6) < 1e-12);
    CHECK_EQ(channel(appearance, "jawWide"), 0.0);
    CHECK_EQ(channel(appearance, "cheekFix"), 0.0);
}

TEST_CASE("avatar.appearance.atomicWardrobeAndHistory") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    CHECK(!appearance.apply(json(R"({"parameters":{"jaw":0.5},"selections":{"hat":"helmet"}})")).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    CHECK(!appearance.canUndo());
    REQUIRE(appearance.apply(json(R"({"selections":{"hair":"short","hat":"helmet"}})")).ok());
    const auto output  = appearance.evaluate();
    const auto visible = encoded(*output.find("visibleParts"));
    CHECK(visible.find("shortHair") == std::string::npos);
    CHECK(visible.find("helmet") != std::string::npos);
    CHECK(visible.find("head") != std::string::npos);
    const auto equipped = encoded(appearance.snapshot());
    REQUIRE(appearance.undo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.redo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), equipped);
    REQUIRE(appearance.apply(json(R"({"selections":{"hair":"short"}})")).ok());
    REQUIRE(appearance.undo().ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.2}})")).ok());
    CHECK(!appearance.canRedo());
}

TEST_CASE("avatar.appearance.rejectInvalidStateAndDefinition") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    const auto initial = encoded(appearance.snapshot());
    for (auto source :
         {R"({"parameters":{"jaw":2}})", R"({"parameters":{"unknown":0}})", R"({"selections":{"missing":""}})",
          R"({"selections":{"hat":"long"}})", R"({"unknown":true})", R"({"parameters":{"jaw":true}})"}) {
        CHECK(!appearance.apply(json(source)).ok());
        CHECK_EQ(encoded(appearance.snapshot()), initial);
    }
    auto nonfinite = json(R"({"parameters":{"jaw":0}})");
    nonfinite.find("parameters")->set("jaw", std::numeric_limits<double>::quiet_NaN());
    CHECK(!appearance.apply(nonfinite).ok());
    for (auto version : {0, 2}) {
        auto invalid = definition();
        invalid.set("version", version);
        CHECK(!appearance.configure(invalid).ok());
        CHECK_EQ(encoded(appearance.snapshot()), initial);
    }
    auto invalid = definition();
    invalid.find("drivers")->at(0).find("inputs")->at(0).set("end", 0.0);
    CHECK(!appearance.configure(invalid).ok());
    CHECK_EQ(encoded(appearance.snapshot()), initial);
    CHECK(!appearance.canUndo());
}

TEST_CASE("avatar.appearance.snapshotRoundTripAndProfileIsolation") {
    eve::avatar::Appearance appearance;
    REQUIRE(appearance.configure(definition()).ok());
    REQUIRE(appearance.apply(json(R"({"parameters":{"jaw":0.7}})")).ok());
    const auto              saved = appearance.snapshot();
    eve::avatar::Appearance restored;
    REQUIRE(restored.configure(definition()).ok());
    REQUIRE(restored.restore(json(encoded(saved))).ok());
    CHECK_EQ(encoded(restored.evaluate()), encoded(appearance.evaluate()));
    auto foreign = saved;
    foreign.set("profile", "other.v1");
    CHECK(!restored.restore(foreign).ok());
    auto future = saved;
    future.set("version", 2);
    CHECK(!restored.restore(future).ok());
    auto incomplete = saved;
    incomplete.set("parameters", eve::Value(eve::Value::Object{}));
    CHECK(!restored.restore(incomplete).ok());
    CHECK_EQ(encoded(restored.snapshot()), encoded(saved));
    for (int i = 0; i < 80; ++i)
        REQUIRE(restored.apply(json(i % 2 ? R"({"parameters":{"jaw":0.1}})" : R"({"parameters":{"jaw":0.2}})")).ok());
    int count = 0;
    while (restored.canUndo()) {
        REQUIRE(restored.undo().ok());
        ++count;
    }
    CHECK_EQ(count, 64);
}

TEST_CASE("avatar.appearance.scriptBoundaryUsesCheckedResults") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        avatarModule <- eve.Avatar();
        appearance <- eve.AvatarAppearance();
        configured <- appearance.configure({schema="eve.appearance.definition",version=1,profile="test.v1",
            parameters=[{id="eye",min=0.0,max=1.0,["default"]=0.0}],drivers=[],slots=[],items=[]}).ok;
        applied <- appearance.apply({parameters={eye=0.7}}).ok;
        saved <- appearance.snapshot();
        rejected <- !appearance.apply({parameters={eye=2.0}}).ok;
        restored <- appearance.restore(saved.value).ok;
        value <- appearance.snapshot().value.parameters.eye;
        graphicsModule <- eve.Graphics();
        sourceMaterial <- graphicsModule.newMaterial();
        targetMaterial <- graphicsModule.newMaterial();
        sourceMaterial.setTint(0.2, 0.3, 0.4, 1.0);
        targetMaterial.setRoughness(0.7);
        copiedPbr <- targetMaterial.copyPbrSurfaceFrom(sourceMaterial).ok;
        targetRoughness <- targetMaterial.getRoughness();
    )"));
    CHECK(vm.find("configured").toBool());
    CHECK(vm.find("applied").toBool());
    CHECK(vm.find("rejected").toBool());
    CHECK(vm.find("restored").toBool());
    CHECK(std::abs(vm.find("value").toFloat() - 0.7f) < 1e-6f);
    CHECK(vm.find("copiedPbr").toBool());
    CHECK(std::abs(vm.find("targetRoughness").toFloat() - 0.7f) < 1e-6f);
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
    REQUIRE(mesh.setMorphWeight("jawWide", 0.1f));
    CHECK(!appearance.applyMorphWeights(mesh, "").ok());  // cheekFix is missing.
    CHECK_EQ(mesh.getMorphWeight("jawWide"), 0.1f);
    REQUIRE(mesh.addMorphTarget("cheekFix", delta));
    REQUIRE(mesh.addMorphTarget("expression", delta));
    REQUIRE(mesh.setMorphWeight("expression", 0.9f));
    REQUIRE(appearance.applyMorphWeights(mesh, "").ok());
    CHECK_EQ(mesh.getMorphWeight("jawWide"), 0.75f);
    CHECK_EQ(mesh.getMorphWeight("cheekFix"), 0.1875f);
    CHECK_EQ(mesh.getMorphWeight("expression"), 0.9f);
    std::vector<float> blended, normals;
    mesh.computeMorphedPositions(blended, normals);
    CHECK(std::abs(blended[2] - 1.8375f) < 1e-6f);
}

TEST_CASE("avatar.appearance.morphNormalsBlendAndRejectAtomically") {
    eve::graphics::Mesh mesh;
    const float         positions[]   = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float         normals[]     = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float         delta[]       = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float         normalDelta[] = {1, 0, -1, 1, 0, -1, 1, 0, -1};
    mesh.initMorphBase(3, positions, normals);
    REQUIRE(mesh.addMorphTarget("jaw", delta));
    REQUIRE(mesh.setMorphNormalDeltas("jaw", normalDelta).ok());
    REQUIRE(mesh.setMorphWeight("jaw", 0.5f));
    const float malformed[] = {1, 2, 3};
    CHECK(!mesh.setMorphNormalDeltas("jaw", malformed).ok());
    CHECK(!mesh.setMorphNormalDeltas("missing", normalDelta).ok());
    std::vector<float> pos, nrm;
    mesh.computeMorphedPositions(pos, nrm);
    CHECK(std::abs(nrm[0] - std::sqrt(0.5f)) < 1e-6f);
    CHECK(std::abs(nrm[2] - std::sqrt(0.5f)) < 1e-6f);
    auto invalid = std::vector<float>(std::begin(normalDelta), std::end(normalDelta));
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!mesh.setMorphNormalDeltas("jaw", invalid).ok());
    REQUIRE(mesh.setMorphNormalDeltas("jaw", {}).ok());
    mesh.computeMorphedPositions(pos, nrm);
    CHECK_EQ(nrm[0], 0.0f);
    CHECK_EQ(nrm[2], 1.0f);
}
