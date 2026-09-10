#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmDocument.h"
#include "common/ECS.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/vulkan/Graphics.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
std::vector<std::byte> vrmGlb(std::string json) {
    while (json.size() % 4) json += ' ';
    std::vector<std::byte> bytes(20 + json.size());
    const uint32_t header[] = {0x46546c67, 2, static_cast<uint32_t>(bytes.size()), static_cast<uint32_t>(json.size()),
                               0x4e4f534a};
    std::memcpy(bytes.data(), header, 20);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    return bytes;
}
const char* minimalVrm = R"({"asset":{"version":"2.0"},"nodes":[{"name":"head"}],
"extensions":{"VRMC_vrm":{"specVersion":"1.0","humanoid":{"humanBones":{"head":{"node":0}}}}}})";
}  // namespace

TEST_CASE("avatar.vrm.parseVersionAndHumanoid") {
    auto bytes  = vrmGlb(minimalVrm);
    auto result = eve::avatar::parseVrm(bytes);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().version, std::string("1.0"));
    REQUIRE_EQ(result.value().humanoid.at("head"), 0);
}

TEST_CASE("avatar.vrm.rejectTruncatedAndUnknownVersion") {
    auto bytes = vrmGlb(minimalVrm);
    bytes.pop_back();
    auto truncated = eve::avatar::parseVrm(bytes);
    REQUIRE(!truncated.ok());
    std::string future = minimalVrm;
    future.replace(future.find("specVersion\":\"1.0") + 14, 3, "9.0");
    auto unknown = eve::avatar::parseVrm(vrmGlb(future));
    REQUIRE(!unknown.ok());
}

TEST_CASE("avatar.vrm.rejectInvalidNodeReference") {
    std::string invalid = minimalVrm;
    invalid.replace(invalid.find("\"node\":0"), 8, "\"node\":7");
    auto result = eve::avatar::parseVrm(vrmGlb(invalid));
    REQUIRE(!result.ok());
}

TEST_CASE("avatar.vrm.failedLoadPreservesPath") {
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("does-not-exist.vrm");
    REQUIRE(!result.ok());
    REQUIRE(avatar.getVroidModelPath().empty());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
}

TEST_CASE("avatar.vrm.importMorphGazeSpringAndRollback") {
    GfxFixture graphics(320, 240);
    auto*      fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm", true));
    const auto directory = (std::filesystem::path(__FILE__).parent_path() / "assets/vrm").string();
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        loaded = avatar.loadVroidModel("contract.vrm");
    REQUIRE(loaded.ok());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 2);
    REQUIRE_EQ(avatar.getVroidSpringCount(), 1);
    REQUIRE_EQ(avatar.getVroidVersion(), std::string("1.0"));
    REQUIRE_EQ(avatar.getHumanoidBoneName("head"), std::string("head"));
    avatar.setExpression("happy");
    avatar.update(1.f / 60);
    avatar.sync();
    REQUIRE_EQ(avatar.getBoundMesh()->getMorphWeight("smile"), 1.f);
    auto* gpu = static_cast<eve::graphics::vulkan::GpuMesh*>(avatar.getBoundMesh()->gpuHandle);
    REQUIRE(gpu->dynamic);
    auto& buffer   = gpu->dynVertices[(gpu->dynamicWriteCount - 1) % gpu->kDynamicVertexCopies];
    auto* vertices = static_cast<eve::graphics::vulkan::MeshVertex*>(buffer.map());
    REQUIRE(vertices != nullptr);
    const float preservedWeight = vertices[0].weights.x;
    buffer.unmap();
    REQUIRE_EQ(preservedWeight, 1.f);
    avatar.setExpression("aa");
    avatar.update(1.f / 60);
    avatar.sync();
    REQUIRE_EQ(avatar.getBoundMesh()->getMorphWeight("smile"), .5f);
    auto before = avatar.sampleAttachmentPoint("leftEye", {0, 0, .1f});
    REQUIRE(before.ok());
    REQUIRE(avatar.setLookAtTarget(2, .4f, 1));
    avatar.update(1.f / 60);
    auto after = avatar.sampleAttachmentPoint("leftEye", {0, 0, .1f});
    REQUIRE(after.ok());
    REQUIRE(std::abs(after.value().x - before.value().x) > 1e-4f);
    auto tip = avatar.sampleAttachmentPoint("hairTip");
    REQUIRE(tip.ok());
    for (int i = 0; i < 30; ++i) avatar.update(1.f / 60);
    auto moved = avatar.sampleAttachmentPoint("hairTip");
    REQUIRE(moved.ok());
    REQUIRE(std::abs(moved.value().x - tip.value().x) > 1e-5f);
    auto*      original = avatar.getRenderable3D();
    const auto handle   = ecs::handle_of(original);
    auto       failed   = avatar.loadVroidModel("missing.vrm");
    REQUIRE(!failed.ok());
    REQUIRE(avatar.getRenderable3D() == original);
    REQUIRE_EQ(avatar.getVroidModelPath(), std::string("contract.vrm"));
    auto replaced = avatar.loadVroidModel("contract.vrm");
    REQUIRE(replaced.ok());
    REQUIRE(ecs::try_get(handle) == nullptr);
    // ECS projection may be destroyed first; owner cleanup must resolve the generation.
    ecs::DestroyEntity(avatar.getRenderable3D());
    avatar.sync();
    avatar.release();
    REQUIRE(fs->unmount(directory));
}

TEST_CASE("avatar.vrm.providerAbsentDoesNotPublish") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm_absent", true));
    REQUIRE(fs->setupWriteDirectory());
    const auto directory = (std::filesystem::path(__FILE__).parent_path() / "assets/vrm").string();
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("contract.vrm");
    REQUIRE(!result.ok());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
    REQUIRE(avatar.getVroidModelPath().empty());
    REQUIRE(fs->unmount(directory));
}
