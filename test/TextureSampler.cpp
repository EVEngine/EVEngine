#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "common/Exception.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/GtsTerrainLodRuntime.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/Procgen.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

std::vector<uint8_t> makeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

}  // namespace

TEST_CASE("TextureSampler.mipmapCountForSize") {
    CHECK_EQ(mipmapCountForSize(1, 1), 1);
    CHECK_EQ(mipmapCountForSize(2, 2), 2);
    CHECK_EQ(mipmapCountForSize(256, 128), 9);
    CHECK_EQ(mipmapCountForSize(64, 64), 7);
}

TEST_CASE("TextureSampler.parseFilterAndMipmap") {
    CHECK_EQ(static_cast<int>(TextureSampler::parseFilter("nearest")),
             static_cast<int>(FilterMode::Nearest));
    CHECK_EQ(static_cast<int>(TextureSampler::parseFilter("linear")),
             static_cast<int>(FilterMode::Linear));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("none")),
             static_cast<int>(MipmapMode::Disabled));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("linear")),
             static_cast<int>(MipmapMode::Linear));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("nearest")),
             static_cast<int>(MipmapMode::Nearest));
}

TEST_CASE("GraphicsSmoke.textureMipmapsAndAnisotropy") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto px = makeSolid(64, 64, 200, 40, 40);

    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true, 8.f);
    Texture *tex = fx.gfx->newTexture(64, 64, px.data(), info);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getMipmapCount(), mipmapCountForSize(64, 64));
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
    CHECK(tex->getSampler().maxAnisotropy >= 8.f - 1e-3f);

    const float deviceMax = fx.gfx->getMaxAnisotropy();
    CHECK(deviceMax >= 1.f);

    // Nearest pixel-art style sampler without rebuilding image.
    fx.gfx->setTextureSampler(tex, TextureSampler::nearest());
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));
    CHECK_EQ(static_cast<int>(tex->getSampler().mag), static_cast<int>(FilterMode::Nearest));

    // Draw once to exercise descriptor rewrite path.
    fx.gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.08f, 1.f));
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
    fx.gfx->drawTexturedRect(tex, 10.f, 10.f, 64.f, 64.f, Color(1, 1, 1, 1));
    fx.gfx->present();
}

TEST_CASE("GraphicsSmoke.setTextureSamplerValidatesStrings") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto px = makeSolid(16, 16, 120, 120, 120);
    Texture *tex = fx.gfx->newTexture(16, 16, px.data());
    REQUIRE(tex != nullptr);

    // Valid string update through the script-facing name.
    fx.gfx->setTextureSampler(tex, "nearest", "none", 1.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));

    fx.gfx->setTextureSampler(tex, "linear", "linear", 8.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));

    // Typos fail fast instead of silently falling back to linear.
    bool threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linar", "none", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linear", "mip", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("GraphicsSmoke.cubemapGeneratesMipChain") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    const int face = 16;
    std::vector<uint8_t> faces(size_t(face) * face * 4 * 6, 128);
    Texture *cube = fx.gfx->newCubemap(face, faces.data());
    REQUIRE(cube != nullptr);
    CHECK_EQ(cube->getMipmapCount(), mipmapCountForSize(face, face));
    CHECK_EQ(static_cast<int>(cube->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
}

TEST_CASE("Renderable3D.meshLodSelectsByDistance") {
    auto *ent = Renderable3D::create();
    REQUIRE(ent != nullptr);

    // Use distinct non-null-ish pointers via cast of integers — only identity matters.
    Mesh *hi = reinterpret_cast<Mesh *>(uintptr_t(0x1000));
    Mesh *mid = reinterpret_cast<Mesh *>(uintptr_t(0x2000));
    Mesh *lo = reinterpret_cast<Mesh *>(uintptr_t(0x3000));

    ent->setMeshLod(0, hi);
    ent->setMeshLod(1, mid, 20.f);
    ent->setMeshLod(2, lo, 50.f);
    CHECK_EQ(ent->getMeshLodCount(), 3);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(0.f), 0);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(19.9f), 0);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(20.f), 1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(49.9f), 1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(50.f), 2);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(999.f), 2);

    auto mr = ent->meshRenderer();
    CHECK(mr->meshForDistance(5.f) == hi);
    CHECK(mr->meshForDistance(25.f) == mid);
    CHECK(mr->meshForDistance(80.f) == lo);
    ent->setMeshLodCullDistance(100.f);CHECK_EQ(ent->getMeshLodCullDistance(),100.f);
    CHECK(mr->meshForDistance(99.9f)==lo);CHECK(mr->meshForDistance(100.f)==nullptr);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(100.f),-1);

    REQUIRE(ent->setMeshLodFadeWidth(0, .25f).ok());
    REQUIRE(ent->setMeshLodFadeWidth(2, .2f).ok());
    REQUIRE(ent->setMeshLodFadePolicy(2, false, .5f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(17.5f), 1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(17.5f) - .5f) < .0001f);
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(95.f), -1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(95.f) - .5f) < .0001f);
    CHECK(!ent->setMeshLodFadeWidth(0, 1.1f).ok());
    CHECK(!ent->setMeshLodFadePolicy(0, true, .5f).ok());

    REQUIRE(ent->setMeshLodFadePolicy(2, true, 1.f).ok());
    REQUIRE(ent->advanceMeshLodTransition(5.f, 0.f).ok());
    REQUIRE(ent->advanceMeshLodTransition(25.f, 0.f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(25.f), 1);
    CHECK_EQ(ent->getMeshLodSecondaryWeightAtDistance(25.f), 0.f);
    REQUIRE(ent->advanceMeshLodTransition(25.f, .25f).ok());
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(25.f) - .25f) < .0001f);
    REQUIRE(ent->advanceMeshLodTransition(25.f, .75f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(25.f), -1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(25.f), 1);
    CHECK(!ent->advanceMeshLodTransition(-1.f, .1f).ok());

    ent->clearMeshLod();
    CHECK_EQ(ent->getMeshLodCount(), 0);
    CHECK(mr->meshForDistance(80.f) == hi);  // falls back to primary mesh
}

TEST_CASE("Renderable3D.gtsTerrainTileUploadsAndConfiguresRealLodChain") {
    using namespace eve::procgen;GfxFixture fx(128,128,true);MeshBuild source;source.setActiveGroup("terrain");
    source.addVertex(0,0,0,0,1,0,0,0);source.addVertex(2,0,0,0,1,0,1,0);
    source.addVertex(2,0,2,0,1,0,1,1);source.addVertex(0,0,2,0,1,0,0,1);
    source.addTriangle(0,1,2);source.addTriangle(0,2,3);
    auto lods=buildGtsTerrainLods(source,1,0,GtsMeshPivot::CenterXZ,defaultGtsTerrainLodLevels());REQUIRE(lods.ok());
    auto* ent=Renderable3D::create();REQUIRE(ent);Procgen procgen;
    auto configured=procgen.configureGtsTerrainTileLods(lods.value(),0,*ent,*fx.gfx,2.f,90.f,10.f,3.f,20.f);REQUIRE(configured.ok());
    CHECK_EQ(ent->getMeshLodCount(),4);CHECK(ent->getMesh()!=nullptr);CHECK(ent->getMeshLodCullDistance()>40.f);
    CHECK(ent->meshRenderer()->meshForDistance(ent->getMeshLodCullDistance())==nullptr);
    CHECK_EQ(ent->transform()->x,10.5f);CHECK_EQ(ent->transform()->y,3.f);CHECK_EQ(ent->transform()->z,21.f);
}

TEST_CASE("Renderable3D.pcgGenericLodsUploadAsOneAtomicChain") {
    using namespace eve::procgen;
    GfxFixture fx(128,128,true);
    MeshBuild source;source.setActiveGroup("surface");
    for(int z=0;z<3;++z)for(int x=0;x<3;++x)
        source.addVertex(float(x),0.f,float(z),0.f,1.f,0.f,float(x)/2.f,float(z)/2.f);
    for(int z=0;z<2;++z)for(int x=0;x<2;++x){const uint32_t a=uint32_t(z*3+x),b=a+1,c=a+3,d=c+1;
        source.addTriangle(a,b,d);source.addTriangle(a,d,c);}
    PcgMeshLodProfile profile;REQUIRE(profile.appendLevel(.8f,.1f,1.f).ok());
    REQUIRE(profile.appendLevel(.2f,.15f,.5f).ok());
    REQUIRE(profile.setFadePolicy(2,false,.5f).ok());
    REQUIRE(profile.setLevelRendererState(0,4,2,false,2,false,4,2).ok());
    REQUIRE(profile.setLevelRendererState(1,2,0,true,0,true,0,0).ok());PcgMeshLodSet lods;
    REQUIRE(buildPcgMeshLodsInto(lods,source,profile).ok());
    auto* ent=Renderable3D::create();REQUIRE(ent);Procgen procgen;
    REQUIRE(procgen.configurePcgMeshLods(lods,*ent,*fx.gfx,2.f,90.f).ok());
    CHECK_EQ(ent->getMeshLodCount(),2);CHECK_EQ(ent->getMeshLodLevelAtDistance(1.f),0);
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(1.1875f),1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(1.1875f)-.5f)<.0001f);
    CHECK_EQ(ent->getMeshLodRendererState(0,0),4);CHECK_EQ(ent->getMeshLodRendererState(0,1),2);
    CHECK_EQ(ent->getMeshLodRendererState(0,2),0);CHECK_EQ(ent->getMeshLodRendererState(1,0),2);
    CHECK(!ent->setMeshLodRendererState(0,3,1,true,1,true,1,1).ok());
    CHECK_EQ(ent->getMeshLodLevelAtDistance(2.f),1);CHECK(ent->getMeshLodCullDistance()>4.9f);
    ssq::VM vm(1024);auto table=vm.addTable("eve");fx.gfx->expose(table);Procgen::expose(table);
    vm.addFunc("pcgBackupTarget",[&](){return ent;});
    vm.run(vm.compileSource(R"(
        local target=pcgBackupTarget();local backup=eve.PcgMeshLodBackup();
        assert(backup.capture(target).ok && backup.isCaptured());
        local id=backup.getEntityId();local generation=backup.getEntityGeneration();
        assert(id==target.getEntityId() && generation==target.getEntityGeneration());
        target.setVisible(false);assert(backup.restore(target).ok && !backup.isCaptured());
        assert(target.getVisible());
    )"));
    const int before=ent->getMeshLodCount();CHECK(!procgen.configurePcgMeshLods(lods,*ent,*fx.gfx,-1.f,90.f).ok());
    CHECK_EQ(ent->getMeshLodCount(),before);ecs::DestroyEntity(ent);
}

TEST_CASE("Renderable3D.gtsTerrainRuntimeReplacesTilesWithStableHandles") {
    using namespace eve::procgen;GfxFixture fx(128,128,true);MeshBuild source;source.setActiveGroup("terrain");
    source.addVertex(0,0,0,0,1,0,0,0);source.addVertex(2,0,0,0,1,0,1,0);source.addVertex(2,0,2,0,1,0,1,1);source.addVertex(0,0,2,0,1,0,0,1);source.addTriangle(0,1,2);source.addTriangle(0,2,3);
    auto lods=buildGtsTerrainLods(source,1,0,GtsMeshPivot::CenterXZ,defaultGtsTerrainLodLevels());REQUIRE(lods.ok());
    Procgen procgen;GtsTerrainLodRuntime runtime;auto first=runtime.replace(lods.value(),procgen,*fx.gfx,2.f,90.f);REQUIRE(first.ok());CHECK_EQ(first.value(),1u);CHECK_EQ(runtime.getTileCount(),2);
    auto* old=runtime.getRenderable(0);REQUIRE(old);auto oldHandle=ecs::handle_of(old);CHECK(old->getVisible());
    CHECK(!runtime.replace(lods.value(),procgen,*fx.gfx,-1.f,90.f).ok());CHECK_EQ(runtime.getRevision(),1u);
    CHECK(ecs::try_get(oldHandle)==old);CHECK(old->getVisible());
    auto* material=fx.gfx->newMaterial();REQUIRE(material);auto applied=runtime.applyMaterial(*material);REQUIRE(applied.ok());CHECK_EQ(applied.value(),2);CHECK(old->getMaterial()==material);
    auto second=runtime.replace(lods.value(),procgen,*fx.gfx,2.f,90.f,5.f,0.f,7.f);REQUIRE(second.ok());CHECK_EQ(second.value(),2u);
    CHECK(ecs::try_get(oldHandle)==nullptr);auto* current=runtime.getRenderable(0);REQUIRE(current);CHECK_EQ(current->transform()->x,5.5f);CHECK_EQ(current->transform()->z,8.f);
    auto cleared=runtime.clear();REQUIRE(cleared.ok());CHECK_EQ(cleared.value(),2);CHECK_EQ(runtime.getTileCount(),0);CHECK_EQ(runtime.getRevision(),3u);
}

TEST_CASE("Renderable3D.gtsTerrainRuntimeRestoresSourceVisibilityAcrossEveryLifetimeOrder") {
    using namespace eve::procgen;GfxFixture fx(128,128,true);MeshBuild sourceMesh;sourceMesh.setActiveGroup("terrain");
    sourceMesh.addVertex(0,0,0,0,1,0,0,0);sourceMesh.addVertex(2,0,0,0,1,0,1,0);sourceMesh.addVertex(2,0,2,0,1,0,1,1);sourceMesh.addVertex(0,0,2,0,1,0,0,1);sourceMesh.addTriangle(0,1,2);sourceMesh.addTriangle(0,2,3);
    auto lods=buildGtsTerrainLods(sourceMesh,1,0,GtsMeshPivot::CenterXZ,defaultGtsTerrainLodLevels());REQUIRE(lods.ok());Procgen procgen;
    auto* firstSource=Renderable3D::create();REQUIRE(firstSource);firstSource->setVisible(true);
    auto* hiddenSource=Renderable3D::create();REQUIRE(hiddenSource);hiddenSource->setVisible(false);
    {
        GtsTerrainLodRuntime runtime;auto first=runtime.replaceAndHideSource(lods.value(),procgen,*fx.gfx,*firstSource,2.f,90.f);REQUIRE(first.ok());
        CHECK(!firstSource->getVisible());CHECK(runtime.getSourceTerrain()==firstSource);CHECK_EQ(runtime.getRevision(),1u);
        CHECK(!runtime.replaceAndHideSource(lods.value(),procgen,*fx.gfx,*firstSource,-1.f,90.f).ok());
        CHECK(!firstSource->getVisible());CHECK(runtime.getSourceTerrain()==firstSource);CHECK_EQ(runtime.getRevision(),1u);
        auto second=runtime.replaceAndHideSource(lods.value(),procgen,*fx.gfx,*hiddenSource,2.f,90.f);REQUIRE(second.ok());
        CHECK(firstSource->getVisible());CHECK(!hiddenSource->getVisible());CHECK(runtime.getSourceTerrain()==hiddenSource);
        auto cleared=runtime.clear();REQUIRE(cleared.ok());CHECK(!hiddenSource->getVisible());CHECK(runtime.getSourceTerrain()==nullptr);
    }
    firstSource->setVisible(true);{
        GtsTerrainLodRuntime runtime;REQUIRE(runtime.replaceAndHideSource(lods.value(),procgen,*fx.gfx,*firstSource,2.f,90.f).ok());CHECK(!firstSource->getVisible());
    }CHECK(firstSource->getVisible());
    auto* destroyedSource=Renderable3D::create();REQUIRE(destroyedSource);destroyedSource->setVisible(true);GtsTerrainLodRuntime sourceFirst;
    REQUIRE(sourceFirst.replaceAndHideSource(lods.value(),procgen,*fx.gfx,*destroyedSource,2.f,90.f).ok());ecs::DestroyEntity(destroyedSource);
    CHECK(sourceFirst.getSourceTerrain()==nullptr);REQUIRE(sourceFirst.clear().ok());
    ecs::DestroyEntity(firstSource);ecs::DestroyEntity(hiddenSource);
}
