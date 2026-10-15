#include "PathBesideSource.h"
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
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
[[maybe_unused]] std::vector<std::byte> vrmGlb(std::string json) {
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


TEST_CASE("avatar.vrm.providerAbsentDoesNotPublish") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm_absent", true));
    REQUIRE(fs->setupWriteDirectory());
    const auto directory = (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "assets/vrm").string();
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("contract.vrm");
    REQUIRE(!result.ok());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
    REQUIRE(avatar.getVroidModelPath().empty());
    REQUIRE(fs->unmount(directory));
}

TEST_CASE("avatar.vrm.graphicsBackendSupportIsExplicit") {
    GfxFixture graphics(320, 240);
    auto*      fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm_backend", true));
    REQUIRE(fs->setupWriteDirectory());
    const auto directory = (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "assets/vrm").string();
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("contract.vrm");
    if (graphics.gfx->getBackendName() == "vulkan") {
        REQUIRE(result.ok());
        REQUIRE_EQ(avatar.getVroidMeshCount(), 2);
    } else {
        REQUIRE(!result.ok());
        REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
        REQUIRE(avatar.getVroidModelPath().empty());
    }
    REQUIRE(fs->unmount(directory));
}
