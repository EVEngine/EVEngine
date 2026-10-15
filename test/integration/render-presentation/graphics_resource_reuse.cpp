#include <filesystem>
#include <memory>
#include "Fixtures.h"
#include "GraphicsParitySupport.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/ViewPreparation.h"
#include "graphics/hair/GroomInstance.h"
#include "zeroerr/unittest.h"
#if !defined(EVENGINE_WEBGPU)
#include "graphics/vulkan/Graphics.h"
#endif

using namespace eve::graphics;

TEST_CASE("graphics.resourceReuse.fileCacheDoesNotReloadResidentPixels") {
    auto      *gfx = parity_test::headlessGraphics();
    TempDir    files;
    const auto path       = files.path() / "eve-resource-reuse.png";
    auto      *filesystem = eve::filesystem::Filesystem::create();
    filesystem->allowMountingForPath(files.path().string());
    REQUIRE(filesystem->mount(files.path().string(), "", false));
    const std::string texturePath = "eve-resource-reuse.png";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    std::filesystem::copy_file("test/assets/resource_reuse.png", path,
                               std::filesystem::copy_options::overwrite_existing);
    auto *texture = gfx->newTextureFromFile(texturePath);
    REQUIRE(texture != nullptr);
    auto desired = TextureSampler::nearest();
    gfx->setTextureSampler(texture, desired);  // Must survive deferred first upload.
    gfx->ensureFileTexturesReady();
    REQUIRE(texture->gpuHandle != nullptr);
    CHECK(texture->sampler == desired);
    auto loaded = gfx->loadTexture(texturePath);
    REQUIRE(loaded.ok());
    CHECK(&loaded.value().get() == texture);
    CHECK(gfx->newTextureFromFile("./" + texturePath) == texture);
    void *const original = texture->gpuHandle;
    std::filesystem::remove(path);
    for (int i = 0; i < 20; ++i) {
        CHECK(gfx->newTextureFromFile(texturePath) == texture);
        CHECK(texture->gpuHandle == original);
    }
    CHECK(!gfx->reloadTextureFromFile(texturePath));
    CHECK(texture->gpuHandle == original);
    auto cached = gfx->loadTexture(texturePath);
    REQUIRE(cached.ok());
    CHECK(&cached.value().get() == texture);
    REQUIRE(gfx->releaseTexture(texture));
    delete texture;
    CHECK(!gfx->loadTexture(texturePath).ok());
    std::filesystem::copy_file("test/assets/resource_reuse.png", path,
                               std::filesystem::copy_options::overwrite_existing);
    auto firstReady = gfx->loadTexture(texturePath);
    REQUIRE(firstReady.ok());
    auto &ready = firstReady.value().get();
    CHECK(!ready.hasDeferredFilePixels());
    CHECK(ready.gpuHandle != nullptr);
    CHECK_EQ(ready.getWidth(), 2);
    CHECK_EQ(ready.getHeight(), 2);
    auto absent = gfx->IResourceFactory::loadTexture(texturePath);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE(gfx->releaseTexture(&ready));
    delete &ready;
    REQUIRE(filesystem->unmount(files.path().string()));
}

TEST_CASE("graphics.resourceReuse.samplerEditsCoalesceAndReuse") {
    GfxFixture    fixture(64, 64);
    auto         *gfx     = fixture.gfx;
    const uint8_t rgba[]  = {255, 0, 0, 255};
    auto         *texture = gfx->newTexture(1, 1, rgba);
    REQUIRE(texture != nullptr);
    auto      *second  = gfx->newTexture(1, 1, rgba);
    const auto initial = texture->sampler;
    auto       nearest = TextureSampler::nearest();
    for (int i = 0; i < 50; ++i) gfx->setTextureSampler(texture, nearest);
#if !defined(EVENGINE_WEBGPU)
    auto      *gpu      = static_cast<vulkan::GpuTexture *>(texture->gpuHandle);
    const auto original = gpu->sampler;
    CHECK(gpu->samplerState == initial);  // Setter has not executed backend work.
    gfx->setTextureSampler(texture, initial);
    gfx->begin3DFrame();
    CHECK(gpu->sampler == original);  // Reverted batch does no work.
    gfx->present();
    gfx->setTextureSampler(texture, nearest);
#endif
    gfx->setTextureSampler(second, nearest);
    gfx->begin3DFrame();
#if !defined(EVENGINE_WEBGPU)
    const auto cached = gpu->sampler;
    CHECK(gpu->samplerState == nearest);
    CHECK(cached == static_cast<vulkan::GpuTexture *>(second->gpuHandle)->sampler);
#endif
    gfx->present();
    gfx->setTextureSampler(texture, initial);
    gfx->begin3DFrame();
    gfx->present();
    gfx->setTextureSampler(texture, nearest);
    gfx->begin3DFrame();
#if !defined(EVENGINE_WEBGPU)
    CHECK(gpu->sampler == cached);
#endif
    gfx->present();
    gfx->setTextureSampler(second, initial);  // Release cancels pending publication.
    REQUIRE(gfx->releaseTexture(second));
    delete second;
    gfx->begin3DFrame();
    gfx->present();
    REQUIRE(gfx->releaseTexture(texture));
    delete texture;
}

TEST_CASE("graphics.resourceReuse.groomPublishesOnceAndKeepsMeshIdentity") {
    GfxFixture             fixture(64, 64);
    auto                  *gfx = fixture.gfx;
    hair::GroomInstance    groom(gfx);
    hair::ProceduralParams params;
    params.strandCount     = 12;
    params.pointsPerStrand = 4;
    params.seed            = 41;
    REQUIRE(groom.bakeProceduralPlane(0.2f, 0.2f, params).ok());
    auto *const mesh = groom.getMesh();
    REQUIRE(mesh != nullptr);
    void *const          gpuIdentity = mesh->gpuHandle;
    hair::GuideSimParams simulation;
    REQUIRE(groom.enableGuideSimulation(simulation).ok());
    for (int i = 0; i < 3; ++i) REQUIRE(groom.update(1.f / 60.f).ok());
    groom.setWidthScale(2.f);
    groom.setWidthScale(3.f);
#if !defined(EVENGINE_WEBGPU)
    auto      *gpu    = static_cast<vulkan::GpuMesh *>(mesh->gpuHandle);
    const auto before = gpu->dynamicWriteCount;
#endif
    auto prepare = [&] { return detail::prepareViewResources(*gfx, glm::mat4(1.f), glm::vec3(0.f)); };
    REQUIRE(prepare().ok());
    CHECK(groom.getMesh() == mesh);
    CHECK(mesh->gpuHandle == gpuIdentity);
#if !defined(EVENGINE_WEBGPU)
    CHECK_EQ(gpu->dynamicWriteCount, before + 1);
#endif
    REQUIRE(prepare().ok());
#if !defined(EVENGINE_WEBGPU)
    CHECK_EQ(gpu->dynamicWriteCount, before + 1);
#endif
    REQUIRE(groom.disableGuideSimulation().ok());
    for (int lod : {1, 2, 0, 1, 0}) {
        groom.setForcedLod(lod);
        REQUIRE(prepare().ok());
        if (lod == 2)
            CHECK(groom.getMesh() == nullptr);
        else
            CHECK(groom.getMesh() == mesh);
        CHECK(mesh->gpuHandle == gpuIdentity);
    }

    hair::GroomGroup unsupported;
    unsupported.name = "unsupported";
    auto strands     = hair::generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    unsupported.strands = std::move(strands).takeValue();
    hair::GroomLod meshLod;
    meshLod.representation = hair::Representation::Meshes;
    unsupported.lods       = {meshLod};
    hair::GroomAsset candidate;
    REQUIRE(candidate.addGroup(std::move(unsupported)).ok());
    const auto curveCount = groom.getCurveCount();
    CHECK(!groom.setAsset(candidate).ok());
    CHECK_EQ(groom.getCurveCount(), curveCount);
    CHECK(groom.getMesh() == mesh);
    CHECK(mesh->gpuHandle == gpuIdentity);
    groom.setWidthScale(4.f);
    REQUIRE(prepare().ok());
    CHECK(groom.getMesh() == mesh);
    gfx->begin3DFrame();
    groom.draw();
    gfx->present();
}
