#include "procgen/GtsMeshSplitter.h"
#include "procgen/GtsMeshSimplifier.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/GtsTerrainExportSettings.h"
#include "procgen/heightmap/Heightmap.h"
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/procgen/GtsTerrainLodPackage.h"
#include "image/ImageData.h"
#include <limits>
#include <zeroerr/unittest.h>
using namespace eve::procgen;

namespace { MeshBuild crossingQuad() {
    MeshBuild m;m.setActiveGroup("terrain");
    m.addVertex(0,0,0,0,1,0,0,0);m.addVertex(2,0,0,0,1,0,1,0);
    m.addVertex(2,0,2,0,1,0,1,1);m.addVertex(0,0,2,0,1,0,0,1);
    m.addTriangle(0,1,2);m.addTriangle(0,2,3);return m;
}}
TEST_CASE("procgen.gtsTerrainBaseMesh.matchesPcgSaveResolutionSampling") {
    Heightmap heightmap(17,17);for(int z=0;z<17;++z)for(int x=0;x<17;++x)heightmap.setHeight(x,z,float(x+z*2)/48.f);
    const int expectedVertices[]={289,81,25,9,4};const int expectedIndices[]={1536,384,96,24,6};
    for(int resolution=0;resolution<=4;++resolution){MeshBuild mesh;
        auto result=buildGtsTerrainBaseMesh(mesh,heightmap,static_cast<GtsTerrainSaveResolution>(resolution),160.f,48.f,80.f);REQUIRE(result.ok());
        CHECK_EQ(mesh.getVertexCount(),expectedVertices[resolution]);CHECK_EQ(mesh.getIndexCount(),expectedIndices[resolution]);
        CHECK_EQ(mesh.getPositionX(mesh.getVertexCount()-1),160.f);CHECK_EQ(mesh.getPositionY(mesh.getVertexCount()-1),48.f);
        CHECK_EQ(mesh.getPositionZ(mesh.getVertexCount()-1),80.f);CHECK_EQ(mesh.getUvU(mesh.getVertexCount()-1),1.f);CHECK_EQ(mesh.getUvV(mesh.getVertexCount()-1),1.f);
        for(int vertex=0;vertex<mesh.getVertexCount();++vertex){const float length=std::sqrt(mesh.getNormalX(vertex)*mesh.getNormalX(vertex)+mesh.getNormalY(vertex)*mesh.getNormalY(vertex)+mesh.getNormalZ(vertex)*mesh.getNormalZ(vertex));CHECK(std::abs(length-1.f)<.0001f);CHECK(mesh.getNormalY(vertex)>0.f);}
        CHECK_EQ(mesh.getMeta("saveResolution",""),std::to_string(resolution));
    }
}

TEST_CASE("procgen.gtsTerrainBaseMesh.rejectsInvalidInputAtomically") {
    Heightmap valid(17,17);MeshBuild output=crossingQuad();const int before=output.getIndexCount();
    CHECK(!buildGtsTerrainBaseMesh(output,valid,static_cast<GtsTerrainSaveResolution>(5),1.f,1.f,1.f).ok());CHECK_EQ(output.getIndexCount(),before);
    Heightmap incompatible(6,6);CHECK(!buildGtsTerrainBaseMesh(output,incompatible,GtsTerrainSaveResolution::Quarter,1.f,1.f,1.f).ok());CHECK_EQ(output.getIndexCount(),before);
    valid.setHeight(3,2,std::numeric_limits<float>::quiet_NaN());CHECK(!buildGtsTerrainBaseMesh(output,valid,GtsTerrainSaveResolution::Full,1.f,1.f,1.f).ok());CHECK_EQ(output.getIndexCount(),before);
}

TEST_CASE("procgen.gtsTerrainLod.convertsHeightmapThroughTheCompletePcgPipeline") {
    Heightmap heightmap(65,65);for(int z=0;z<65;++z)for(int x=0;x<65;++x)heightmap.setHeight(x,z,float(x+z)/128.f);
    GtsTerrainLodSet lods;auto converted=buildDefaultGtsTerrainLodsFromHeightmapInto(lods,heightmap,
        GtsTerrainSaveResolution::Quarter,160.f,32.f,80.f,3,GtsMeshPivot::None);
    if(!converted)throw std::runtime_error(converted.error()->message());
    CHECK_EQ(lods.getColumnCount(),4);CHECK_EQ(lods.getRowCount(),4);CHECK_EQ(lods.getTileCount(),16);CHECK_EQ(lods.getLevelCount(),4);
    for(int tile=0;tile<lods.getTileCount();++tile){const auto* value=lods.tileAt(tile);REQUIRE(value);CHECK_EQ(value->levels.size(),4u);CHECK(!value->levels.front().empty());}
    auto snapshot=lods.snapshotJson();REQUIRE(snapshot.ok());
    CHECK(!buildDefaultGtsTerrainLodsFromHeightmapInto(lods,heightmap,GtsTerrainSaveResolution::Quarter,
        160.f,32.f,80.f,6,GtsMeshPivot::None).ok());
    auto unchanged=lods.snapshotJson();REQUIRE(unchanged.ok());CHECK_EQ(unchanged.value(),snapshot.value());
}

TEST_CASE("procgen.gtsTerrainMeshSettings.persistAndDriveCustomLodCount") {
    GtsTerrainMeshSettings settings;CHECK_EQ(settings.getSaveResolution(),0);CHECK_EQ(settings.getLodCount(),4);
    CHECK_EQ(settings.getSubTiles(),3);CHECK_EQ(settings.getLodQuality(3),12.5f);
    REQUIRE(settings.setSaveResolution(2).ok());REQUIRE(settings.setLodCount(2).ok());REQUIRE(settings.setSubTiles(1).ok());
    REQUIRE(settings.setLodQuality(0,75.f).ok());REQUIRE(settings.setLodQuality(1,0.f).ok());
    CHECK_EQ(settings.getLodTransitionHeight(0),.95f);CHECK_EQ(settings.getLodTransitionHeight(1),.02f);
    auto json=settings.snapshotJson();REQUIRE(json.ok());GtsTerrainMeshSettings restored;REQUIRE(restored.restoreJson(json.value()).ok());
    auto again=restored.snapshotJson();REQUIRE(again.ok());CHECK_EQ(again.value(),json.value());
    Heightmap heightmap(17,17);for(int z=0;z<17;++z)for(int x=0;x<17;++x)heightmap.setHeight(x,z,float(x+z)/32.f);
    GtsTerrainLodSet lods;REQUIRE(buildGtsTerrainLodsFromHeightmapInto(lods,heightmap,restored,16.f,8.f,16.f).ok());
    CHECK_EQ(lods.getTileCount(),4);CHECK_EQ(lods.getLevelCount(),2);for(int tile=0;tile<4;++tile)CHECK(!lods.tileAt(tile)->levels[1].empty());
    std::string unknown=json.value();unknown.insert(1,"\"unknown\":1,");CHECK(!restored.restoreJson(unknown).ok());
    again=restored.snapshotJson();REQUIRE(again.ok());CHECK_EQ(again.value(),json.value());
    CHECK(!restored.setLodCount(0).ok());CHECK(!restored.setLodQuality(4,50.f).ok());CHECK(!restored.setSubTiles(6).ok());
}
TEST_CASE("procgen.gtsMeshSplitter.clipsAcrossCellsAndInterpolatesAttributes") {
    auto result=splitGtsMesh(crossingQuad(),1,1,GtsMeshPivot::None);REQUIRE(result.ok());
    CHECK_EQ(result.value().getColumnCount(),2);CHECK_EQ(result.value().getRowCount(),2);CHECK_EQ(result.value().getTileCount(),4);
    for(int i=0;i<4;++i){auto* tile=result.value().tileAt(i);REQUIRE(tile);CHECK(!tile->mesh.empty());
        for(int v=0;v<tile->mesh.getVertexCount();++v){CHECK(tile->mesh.getUvU(v)>=0.f);CHECK(tile->mesh.getUvU(v)<=1.f);
            CHECK(tile->mesh.getUvV(v)>=0.f);CHECK(tile->mesh.getUvV(v)<=1.f);CHECK_EQ(tile->mesh.getNormalY(v),1.f);}}
}
TEST_CASE("procgen.gtsMeshSplitter.translatesPivotAndPreservesWorldBounds") {
    auto result=splitGtsMesh(crossingQuad(),1,0,GtsMeshPivot::CenterXZ);REQUIRE(result.ok());
    auto* right=result.value().tileAt(1);REQUIRE(right);CHECK_EQ(right->offsetX,1.5f);CHECK_EQ(right->offsetZ,1.f);
    float min=99,max=-99;for(int i=0;i<right->mesh.getVertexCount();++i){float world=right->mesh.getPositionX(i)+right->offsetX;min=std::min(min,world);max=std::max(max,world);}
    CHECK_EQ(min,1.f);CHECK_EQ(max,2.f);
}
TEST_CASE("procgen.gtsMeshSplitter.rejectsInvalidInput") {
    MeshBuild invalid=crossingQuad();invalid.positions()[0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!splitGtsMesh(invalid,1,1).ok());CHECK(!splitGtsMesh(crossingQuad(),-1,1).ok());
    MeshBuild empty;CHECK(!splitGtsMesh(empty,0,0).ok());
}
TEST_CASE("procgen.gtsMeshSimplifier.reducesInteriorAndPreservesBorder") {
    MeshBuild grid;grid.setActiveGroup("terrain");for(int z=0;z<3;++z)for(int x=0;x<3;++x)grid.addVertex(float(x),x==1&&z==1?.2f:0.f,float(z),0,1,0,x/2.f,z/2.f);
    for(int z=0;z<2;++z)for(int x=0;x<2;++x){uint32_t a=z*3+x,b=a+1,c=a+3,d=c+1;grid.addTriangle(a,b,d);grid.addTriangle(a,d,c);}
    MeshBuild simplified;GtsMeshSimplificationOptions options;options.preserveBorderEdges=false;
    auto result=simplifyGtsMesh(simplified,grid,.5f,options);REQUIRE(result.ok());CHECK(result.value()<=4);CHECK(result.value()>0);
    CHECK_EQ(simplified.getGroupCount(),1);CHECK_EQ(simplified.getGroupName(0),"terrain");
    MeshBuild border;options.preserveBorderEdges=true;REQUIRE(simplifyGtsMesh(border,grid,.1f,options).ok());CHECK(border.getIndexCount()/3>=4);
}
TEST_CASE("procgen.gtsMeshSimplifier.aliasAndFailureAreAtomic") {
    MeshBuild mesh=crossingQuad();auto ok=simplifyGtsMesh(mesh,mesh,.5f,{});REQUIRE(ok.ok());CHECK(!mesh.empty());
    MeshBuild output=crossingQuad();const int before=output.getIndexCount();GtsMeshSimplificationOptions invalid;invalid.maxIterationCount=0;
    CHECK(!simplifyGtsMesh(output,crossingQuad(),.5f,invalid).ok());CHECK_EQ(output.getIndexCount(),before);
}

TEST_CASE("procgen.gtsMeshSimplifier.smartLinksSplitAttributesWithoutMergingThem") {
    MeshBuild split;split.setActiveGroup("terrain");
    split.addVertex(0,0,0,0,1,0,0,0);split.addVertex(1,0,0,0,1,0,1,0);
    split.addVertex(1,0,1,0,1,0,1,1);split.addVertex(0,0,1,0,1,0,0,1);
    split.addVertex(1,0,0,0,1,0,0,0);split.addVertex(2,0,0,0,1,0,1,0);
    split.addVertex(2,0,1,0,1,0,1,1);split.addVertex(1,0,1,0,1,0,0,1);
    split.addTriangle(0,1,2);split.addTriangle(0,2,3);split.addTriangle(4,5,6);split.addTriangle(4,6,7);
    GtsMeshSimplificationOptions options;options.preserveBorderEdges=true;options.enableSmartLink=false;
    MeshBuild disconnected;REQUIRE(simplifyGtsMesh(disconnected,split,.5f,options).ok());CHECK_EQ(disconnected.getIndexCount()/3,4);
    options.enableSmartLink=true;MeshBuild linked;REQUIRE(simplifyGtsMesh(linked,split,.5f,options).ok());
    CHECK_EQ(linked.getIndexCount()/3,2);
    bool retainedSplitAttributes=false;for(int a=0;a<linked.getVertexCount();++a)for(int b=a+1;b<linked.getVertexCount();++b)
        if(linked.getPositionX(a)==linked.getPositionX(b)&&linked.getPositionZ(a)==linked.getPositionZ(b)&&
           (linked.getUvU(a)!=linked.getUvU(b)||linked.getUvV(a)!=linked.getUvV(b)))retainedSplitAttributes=true;
    CHECK(retainedSplitAttributes);
    options.preserveUvSeamEdges=true;MeshBuild seam;REQUIRE(simplifyGtsMesh(seam,split,.5f,options).ok());
    CHECK_EQ(seam.getIndexCount()/3,4);
    MeshBuild folded=split;folded.uvs()[8]=1.f;folded.uvs()[9]=0.f;folded.uvs()[14]=1.f;folded.uvs()[15]=1.f;
    options.preserveUvSeamEdges=false;options.preserveUvFoldoverEdges=true;MeshBuild foldover;
    REQUIRE(simplifyGtsMesh(foldover,folded,.5f,options).ok());CHECK_EQ(foldover.getIndexCount()/3,4);
}

TEST_CASE("procgen.gtsTerrainLod.buildsSequentialLevelsForEveryTile") {
    auto levels=defaultGtsTerrainLodLevels();
    auto result=buildGtsTerrainLods(crossingQuad(),1,0,GtsMeshPivot::CenterXZ,levels);
    REQUIRE(result.ok());CHECK_EQ(result.value().getColumnCount(),2);CHECK_EQ(result.value().getTileCount(),2);
    CHECK_EQ(result.value().getLevelCount(),4);
    for(int tileIndex=0;tileIndex<result.value().getTileCount();++tileIndex){
        auto* tile=result.value().tileAt(tileIndex);REQUIRE(tile);CHECK_EQ(tile->levels.size(),4u);
        for(size_t level=1;level<tile->levels.size();++level)
            CHECK(tile->levels[level].getIndexCount()<=tile->levels[level-1].getIndexCount());
    }
    CHECK_EQ(result.value().levelAt(0)->quality,1.f);
    CHECK_EQ(result.value().levelAt(3)->screenRelativeTransitionHeight,.02f);
}

TEST_CASE("procgen.gtsTerrainLod.rejectsInvalidProfileWithoutPartialResult") {
    auto levels=defaultGtsTerrainLodLevels();levels[1].screenRelativeTransitionHeight=.96f;
    CHECK(!buildGtsTerrainLods(crossingQuad(),0,0,GtsMeshPivot::None,levels).ok());
    levels=defaultGtsTerrainLodLevels();levels[2].quality=0.f;
    CHECK(!buildGtsTerrainLods(crossingQuad(),0,0,GtsMeshPivot::None,levels).ok());
}

TEST_CASE("procgen.gtsTerrainLod.selectsUnityScreenThresholdsAndRenderableDistances") {
    auto result=buildGtsTerrainLods(crossingQuad(),1,0,GtsMeshPivot::None,defaultGtsTerrainLodLevels());REQUIRE(result.ok());
    const auto& lod=result.value();CHECK_EQ(lod.selectLevel(.96f),0);CHECK_EQ(lod.selectLevel(.8f),1);
    CHECK_EQ(lod.selectLevel(.65f),2);CHECK_EQ(lod.selectLevel(.1f),3);CHECK_EQ(lod.selectLevel(.01f),-1);
    const float switch1=lod.getLevelSwitchDistance(1,2.f,90.f);CHECK(std::abs(switch1-(1.f/.7f))<.0001f);
    CHECK_EQ(lod.selectLevelForCamera(switch1*.99f,2.f,90.f),1);
    CHECK_EQ(lod.selectLevelForCamera(switch1*1.01f,2.f,90.f),2);
    CHECK_EQ(lod.selectLevelForCamera(-1.f,2.f,90.f),-2);CHECK_EQ(lod.getLevelSwitchDistance(9,2.f,90.f),-1.f);
}

TEST_CASE("procgen.gtsTerrainLod.codecRoundTripsAndRejectsAtomically") {
    auto built=buildGtsTerrainLods(crossingQuad(),1,0,GtsMeshPivot::CenterXZ,defaultGtsTerrainLodLevels());REQUIRE(built.ok());
    auto json=built.value().snapshotJson();REQUIRE(json.ok());GtsTerrainLodSet restored;REQUIRE(restored.restoreJson(json.value()).ok());
    auto again=restored.snapshotJson();REQUIRE(again.ok());CHECK_EQ(again.value(),json.value());
    auto* tile=restored.tileAt(0);REQUIRE(tile);CHECK_EQ(tile->levels.size(),4u);CHECK_EQ(tile->levels[0].getGroupName(0),"terrain");
    std::string unknown=json.value();unknown.insert(1,"\"extra\":1,");CHECK(!restored.restoreJson(unknown).ok());
    auto unchanged=restored.snapshotJson();REQUIRE(unchanged.ok());CHECK_EQ(unchanged.value(),json.value());
    std::string wrong=json.value();auto pos=wrong.find("eve.procgen.gts-terrain-lod");REQUIRE(pos!=std::string::npos);
    wrong.replace(pos,28,"eve.procgen.wrong-terrain-lod");CHECK(!restored.restoreJson(wrong).ok());
    unchanged=restored.snapshotJson();REQUIRE(unchanged.ok());CHECK_EQ(unchanged.value(),json.value());
}

TEST_CASE("procgen.gtsTerrainLod.plansDeterministicNativeAssetNames") {
    auto lods=buildGtsTerrainLods(crossingQuad(),1,0,GtsMeshPivot::None,defaultGtsTerrainLodLevels());REQUIRE(lods.ok());
    auto entries=planGtsTerrainLodAssets(lods.value(),"Alpine");REQUIRE(entries.ok());CHECK_EQ(entries.value().size(),8u);
    CHECK_EQ(entries.value()[0].objectName,"Alpine SubTile0_LOD_0");CHECK_EQ(entries.value()[0].meshName,"Alpine_SubTile_00");
    CHECK_EQ(entries.value()[0].relativePath,"Meshes/Alpine_SubTile_00.eve.mesh.json");
    CHECK_EQ(entries.value()[7].relativePath,"Meshes/Alpine_SubTile_13.eve.mesh.json");
    GtsTerrainLodAssetPlan plan;REQUIRE(planGtsTerrainLodAssetsInto(plan,lods.value(),"Alpine","Generated/Meshes").ok());
    CHECK_EQ(plan.getEntryCount(),8);CHECK_EQ(plan.getTileIndex(7),1);CHECK_EQ(plan.getLevelIndex(7),3);
    const auto before=plan.getRelativePath(0);CHECK(!planGtsTerrainLodAssetsInto(plan,lods.value(),"../bad","Meshes").ok());CHECK_EQ(plan.getRelativePath(0),before);
}

TEST_CASE("asset.procgen.gtsTerrainLod.packagesCanonicalMeshesEndToEnd") {
    auto lods=buildGtsTerrainLods(crossingQuad(),1,0,GtsMeshPivot::None,defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    eve::asset_import::ImportPackageIdentity identity{
        *eve::PersistentId::parse("3c615db5-e833-4dd3-a537-e60e9d79e259"),"pcg.gts.terrain","1.0.0",{}};
    auto prepared=eve::asset_procgen::prepareGtsTerrainLodPackage(identity,lods.value(),"Alpine");
    REQUIRE(prepared.ok());
    CHECK_EQ(prepared.value().manifest.assets.size(),8u);
    CHECK_EQ(prepared.value().manifest.entrypoints.size(),9u);
    CHECK_EQ(prepared.value().sourceMappings.front().sourceObject,"Alpine SubTile0_LOD_0");
    auto archiveBytes=eve::asset::buildEvaArchive(prepared.value().manifest,prepared.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive=eve::asset::parseEvaArchive(archiveBytes.value()); REQUIRE(archive.ok());
    auto profile=eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan"); REQUIRE(profile.ok());
    auto cooked=eve::asset::cookEvaToEvpack(archive.value(),profile.value()); REQUIRE(cooked.ok());
    auto pack=eve::asset::parseEvpack(cooked.value().bytes); REQUIRE(pack.ok());
    eve::asset::EvpackResourceReader reader(std::make_shared<const eve::asset::Evpack>(std::move(pack).takeValue()));
    eve::asset::EvpackCapabilities caps{"windows","x86_64","vulkan",{"rgba8"},{"spirv-1.6"},{"high"},{}};
    auto payload=reader.read(prepared.value().manifest.entrypoints.at("tile-1-lod-3"),"eve.mesh/3",caps,1024*1024);
    REQUIRE(payload.ok());
    bool decoded=false;
    for(const auto& chunk:payload.value().chunks) if(chunk.kind==eve::asset::EvpackChunkKind::Bulk) {
        auto mesh=eve::asset::decodeCanonicalMesh(chunk.bytes); REQUIRE(mesh.ok());
        CHECK_EQ(mesh.value().positions,lods.value().tileAt(1)->levels[3].positions()); decoded=true;
    }
    REQUIRE(decoded);
    auto limited=identity; eve::asset_import::AssetImportLimits limits; limits.maximumAssets=1;
    REQUIRE(!eve::asset_procgen::prepareGtsTerrainLodPackage(limited,lods.value(),"Alpine",limits).ok());
}

TEST_CASE("procgen.gtsTerrainExportSettings.matchesOriginalPresetsAndRoundTrips") {
    auto impostor0=makeGtsTerrainImpostorLod(0),impostor2=makeGtsTerrainImpostorLod(2);
    auto low0=makeGtsTerrainLowPolyLod(0),low1=makeGtsTerrainLowPolyLod(1);
    REQUIRE(impostor0.ok());REQUIRE(impostor2.ok());REQUIRE(low0.ok());REQUIRE(low1.ok());
    CHECK_EQ(int(impostor0.value().saveResolution),1);CHECK_EQ(impostor0.value().textureExportResolution,2048);
    CHECK_EQ(int(impostor2.value().saveResolution),3);CHECK_EQ(impostor2.value().textureExportResolution,512);
    CHECK(!impostor0.value().bakeVertexColors);CHECK(impostor0.value().exportNormalMaps);
    CHECK_EQ(int(low0.value().saveResolution),3);CHECK_EQ(int(low1.value().saveResolution),4);
    CHECK_EQ(int(low0.value().normalEdgeMode),1);CHECK(!low0.value().exportNormalMaps);
    CHECK_EQ(int(low0.value().materialShader),1);CHECK_EQ(low0.value().namePrefix,"LOD0_");
    auto first=impostor0.value();first.screenRelativeTransitionHeight=.8f;
    auto second=impostor2.value();second.screenRelativeTransitionHeight=.2f;second.simplifyQuality=.5f;
    GtsTerrainExportSettings settings;REQUIRE(settings.setSourceLods({first,second}).ok());
    REQUIRE(settings.setImpostorLods({low0.value()}).ok());CHECK_EQ(settings.getSourceLodCount(),2);
    auto compiled=settings.compileSourceLevels();REQUIRE(compiled.ok());CHECK_EQ(compiled.value().size(),2u);
    CHECK_EQ(compiled.value()[1].quality,.5f);CHECK_EQ(compiled.value()[1].screenRelativeTransitionHeight,.2f);
    auto json=settings.snapshotJson();REQUIRE(json.ok());GtsTerrainExportSettings restored;
    REQUIRE(restored.restoreJson(json.value()).ok());auto again=restored.snapshotJson();REQUIRE(again.ok());
    CHECK_EQ(again.value(),json.value());CHECK_EQ(restored.impostorLodAt(0)->vertexColorSmoothing,3);
    std::string unknown=json.value();unknown.insert(1,"\"unknown\":1,");CHECK(!restored.restoreJson(unknown).ok());
    again=restored.snapshotJson();REQUIRE(again.ok());CHECK_EQ(again.value(),json.value());
    second.screenRelativeTransitionHeight=.9f;CHECK(!restored.setSourceLods({first,second}).ok());
    CHECK_EQ(restored.getSourceLodCount(),2);CHECK(!makeGtsTerrainImpostorLod(-1).ok());
    GtsTerrainExportWorkflow workflow;workflow.action=GtsTerrainConversionAction::ColliderOnly;
    workflow.colliderType=GtsTerrainColliderType::Mesh;workflow.colliderResolution=GtsTerrainSaveResolution::Quarter;
    workflow.colliderSimplifyQuality=.35f;workflow.copyPcgObjects=false;workflow.addTreeColliders=false;
    workflow.impostorRange=5000.0;REQUIRE(restored.configureWorkflow(workflow).ok());
    auto workflowJson=restored.snapshotJson();REQUIRE(workflowJson.ok());GtsTerrainExportSettings workflowRestored;
    REQUIRE(workflowRestored.restoreJson(workflowJson.value()).ok());auto decoded=workflowRestored.getWorkflow();
    CHECK_EQ(int(decoded.action),1);CHECK_EQ(int(decoded.colliderType),0);CHECK_EQ(int(decoded.colliderResolution),2);
    CHECK_EQ(decoded.colliderSimplifyQuality,.35f);CHECK(!decoded.copyPcgObjects);CHECK_EQ(decoded.impostorRange,5000.0);
    auto before=workflowRestored.snapshotJson();REQUIRE(before.ok());workflow.impostorRange=-1.0;
    CHECK(!workflowRestored.configureWorkflow(workflow).ok());auto after=workflowRestored.snapshotJson();REQUIRE(after.ok());
    CHECK_EQ(after.value(),before.value());
    std::string version2=workflowJson.value();auto face=version2.find("\"objFaceMode\":0,");REQUIRE(face!=std::string::npos);
    version2.erase(face,std::string("\"objFaceMode\":0,").size());auto version=version2.find("\"version\":3");
    REQUIRE(version!=std::string::npos);version2.replace(version,std::string("\"version\":3").size(),"\"version\":2");
    GtsTerrainExportSettings migrated;REQUIRE(migrated.restoreJson(version2).ok());CHECK_EQ(int(migrated.getWorkflow().objFaceMode),0);
    auto migratedJson=migrated.snapshotJson();REQUIRE(migratedJson.ok());CHECK(migratedJson.value().find("\"version\":3")!=std::string::npos);
}

TEST_CASE("procgen.gtsTerrainExportSettings.drivesSourceAndColliderGeometry") {
    Heightmap heightmap(17,17);for(int z=0;z<17;++z)for(int x=0;x<17;++x)
        heightmap.setHeight(x,z,float(x+2*z)/48.f);
    auto source=makeGtsTerrainImpostorLod(1);REQUIRE(source.ok());
    source.value().screenRelativeTransitionHeight=.02f;source.value().simplifyQuality=1.f;
    GtsTerrainExportSettings settings;REQUIRE(settings.setSourceLods({source.value()}).ok());
    GtsTerrainExportWorkflow workflow;workflow.colliderType=GtsTerrainColliderType::Mesh;
    workflow.colliderResolution=GtsTerrainSaveResolution::Quarter;workflow.colliderSimplifyQuality=.5f;
    REQUIRE(settings.configureWorkflow(workflow).ok());
    GtsTerrainLodSet lods;REQUIRE(buildGtsTerrainExportLodsFromHeightmapInto(
        lods,heightmap,settings,16.f,8.f,16.f,1,GtsMeshPivot::CenterXZ).ok());
    CHECK_EQ(lods.getTileCount(),4);CHECK_EQ(lods.getLevelCount(),1);
    for(int tile=0;tile<4;++tile){REQUIRE(lods.tileAt(tile));CHECK(!lods.tileAt(tile)->levels[0].empty());}
    MeshBuild collider;REQUIRE(buildGtsTerrainColliderMeshFromHeightmapInto(
        collider,heightmap,workflow,16.f,8.f,16.f).ok());CHECK(!collider.empty());CHECK(collider.getVertexCount()<=25);
    const int before=collider.getIndexCount();workflow.colliderType=GtsTerrainColliderType::Heightfield;
    CHECK(!buildGtsTerrainColliderMeshFromHeightmapInto(collider,heightmap,workflow,16.f,8.f,16.f).ok());
    CHECK_EQ(collider.getIndexCount(),before);
}

TEST_CASE("procgen.gtsTerrainObj.matchesPcgCoordinatesUvsAndFaces") {
    Heightmap heightmap(3,3);heightmap.setHeight(2,2,1.f);
    auto triangles=encodeGtsTerrainObj(heightmap,GtsTerrainSaveResolution::Full,4.f,3.f,2.f,GtsTerrainObjFaceMode::Triangles);
    REQUIRE(triangles.ok());const auto& tri=triangles.value();CHECK(tri.starts_with("# Unity terrain OBJ File\n"));
    auto count=[](const std::string& text,const std::string& token){int total=0;std::size_t at=0;while((at=text.find(token,at))!=std::string::npos){++total;at+=token.size();}return total;};
    CHECK_EQ(count(tri,"\nv "),9);CHECK_EQ(count(tri,"\nvt "),9);CHECK_EQ(count(tri,"\nf "),8);
    CHECK(tri.find("v -2 3 4\n")!=std::string::npos);CHECK(tri.find("vt 1 1\n")!=std::string::npos);
    CHECK(tri.find("f 1/1 4/4 2/2\n")!=std::string::npos);
    auto quads=encodeGtsTerrainObj(heightmap,GtsTerrainSaveResolution::Full,4.f,3.f,2.f,GtsTerrainObjFaceMode::Quads);
    REQUIRE(quads.ok());CHECK_EQ(count(quads.value(),"\nf "),4);CHECK(quads.value().find("f 1/1 4/4 5/5 2/2\n")!=std::string::npos);
    CHECK(!encodeGtsTerrainObj(heightmap,GtsTerrainSaveResolution::Full,4.f,3.f,2.f,static_cast<GtsTerrainObjFaceMode>(2)).ok());
}

TEST_CASE("procgen.gtsTerrainObj.maskedMatchesPcgCellClassificationAndWinding") {
    Heightmap terrain(3,3);for(int z=0;z<3;++z)for(int x=0;x<3;++x)terrain.setHeight(x,z,float(x+z*3)/8.f);
    Heightmap mask(2,2);mask.setHeight(0,0,0.f);mask.setHeight(1,0,1.f);mask.setHeight(0,1,1.f);mask.setHeight(1,1,1.f);
    auto inside=encodeGtsMaskedTerrainObj(terrain,mask,GtsTerrainSaveResolution::Full,2.f,8.f,2.f,
        GtsTerrainObjFaceMode::Triangles,.2f,false);
    auto outside=encodeGtsMaskedTerrainObj(terrain,mask,GtsTerrainSaveResolution::Full,2.f,8.f,2.f,
        GtsTerrainObjFaceMode::Quads,.2f,true);
    REQUIRE(inside.ok());REQUIRE(outside.ok());
    auto count=[](const std::string& text,const std::string& token){int total=0;std::size_t at=0;while((at=text.find(token,at))!=std::string::npos){++total;at+=token.size();}return total;};
    CHECK_EQ(count(inside.value(),"\nf "),6);CHECK_EQ(count(outside.value(),"\nf "),1);
    CHECK(outside.value().find("v 0 3 -0")!=std::string::npos);
    CHECK(outside.value().find("vt 0 0.333333343")!=std::string::npos);
    CHECK(outside.value().find("f 1/1 3/3 4/4 2/2\n")!=std::string::npos);
    CHECK(!encodeGtsMaskedTerrainObj(terrain,mask,GtsTerrainSaveResolution::Full,2.f,8.f,2.f,
        GtsTerrainObjFaceMode::Triangles,1.1f,false).ok());
    Heightmap empty;CHECK(!encodeGtsMaskedTerrainObj(terrain,empty,GtsTerrainSaveResolution::Full,2.f,8.f,2.f,
        GtsTerrainObjFaceMode::Triangles,.2f,false).ok());
}

TEST_CASE("procgen.gtsTerrainVertexColors.matchesPcgSmoothAndSharpBake") {
    MeshBuild source=crossingQuad();eve::image::ImageData texture(2,2,"RGBA32F");
    texture.setPixel(0,0,{1,0,0,1});texture.setPixel(1,0,{0,1,0,1});
    texture.setPixel(0,1,{0,0,1,1});texture.setPixel(1,1,{1,1,1,1});
    MeshBuild smooth;auto smoothResult=bakeGtsTerrainVertexColorsInto(smooth,source,texture,
        GtsTerrainNormalEdgeMode::Smooth,0,2.f,2.f,false);
    REQUIRE(smoothResult.ok());CHECK_EQ(smoothResult.value(),4);CHECK(smooth.hasVertexColors());
    CHECK_EQ(smooth.getColor(0,0),1.f);CHECK_EQ(smooth.getColor(0,1),0.f);
    CHECK_EQ(smooth.getColor(2,0),1.f);CHECK_EQ(smooth.getColor(2,1),1.f);CHECK_EQ(smooth.getColor(2,2),1.f);
    MeshBuild sharp;auto sharpResult=bakeGtsTerrainVertexColorsInto(sharp,source,texture,
        GtsTerrainNormalEdgeMode::Sharp,0,2.f,2.f,false);
    REQUIRE(sharpResult.ok());CHECK_EQ(sharpResult.value(),6);CHECK_EQ(sharp.getIndexCount(),6);
    CHECK_EQ(sharp.getIndex(0),0);CHECK_EQ(sharp.getIndex(5),5);CHECK(sharp.hasVertexColors());
    CHECK(std::abs(sharp.getColor(0,0)-2.f/3.f)<1e-6f);CHECK(std::abs(sharp.getColor(0,1)-2.f/3.f)<1e-6f);
    CHECK(std::abs(sharp.getColor(0,2)-1.f/3.f)<1e-6f);CHECK_EQ(sharp.getColor(0,0),sharp.getColor(2,0));
    auto copied=sharp.copyGroup(0);REQUIRE(copied);CHECK(copied->hasVertexColors());
    auto split=splitGtsMesh(sharp,1,1,GtsMeshPivot::None);REQUIRE(split.ok());
    for(int tile=0;tile<split.value().getTileCount();++tile){const auto* part=split.value().tileAt(tile);
        REQUIRE(part);if(!part->mesh.empty())CHECK(part->mesh.hasVertexColors());}
    MeshBuild simplified;REQUIRE(simplifyGtsMesh(simplified,sharp,1.f).ok());CHECK(simplified.hasVertexColors());
    std::vector<GtsTerrainLodLevelSettings> levels{{1.f,.02f,{}}};
    auto lods=buildGtsTerrainLods(sharp,0,0,GtsMeshPivot::None,levels);REQUIRE(lods.ok());
    auto encoded=lods.value().snapshotJson();REQUIRE(encoded.ok());CHECK(encoded.value().find("\"version\":2")!=std::string::npos);
    GtsTerrainLodSet restored;REQUIRE(restored.restoreJson(encoded.value()).ok());
    const auto* restoredTile=restored.tileAt(0);REQUIRE(restoredTile);REQUIRE_EQ(restoredTile->levels.size(),1);
    CHECK(restoredTile->levels[0].hasVertexColors());CHECK_EQ(restoredTile->levels[0].colors(),sharp.colors());
    const auto before=smooth.colors();CHECK(!bakeGtsTerrainVertexColorsInto(smooth,source,texture,
        static_cast<GtsTerrainNormalEdgeMode>(2),0,2.f,2.f,false).ok());CHECK_EQ(smooth.colors(),before);
}
