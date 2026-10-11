#include "animation/AnimSkin.h"
#include "Fixtures.h"
#include "graphics/Graphics.h"
#include "procgen/Procgen.h"
#include "procgen/animation/PcgSkinnedMesh.h"
#include "procgen/animation/ProcgenAnimation.h"

#include <limits>
#include <functional>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <zeroerr/unittest.h>

using namespace eve;

namespace {
procgen::MeshBuild triangle(const char* material, float offset) {
    procgen::MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(offset,0.F,0.F,1.F,0.F,0.F,0.F,0.F);
    mesh.addVertex(offset+1.F,0.F,0.F,1.F,0.F,0.F,1.F,0.F);
    mesh.addVertex(offset,1.F,0.F,1.F,0.F,0.F,0.F,1.F);
    mesh.addTriangle(0,1,2);
    return mesh;
}

std::array<float,16> identityBind() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

std::unique_ptr<animation::AnimSkin> skinFor(const procgen::MeshBuild& mesh,
                                             std::vector<int> bones,
                                             std::vector<std::array<float,16>> binds,
                                             std::vector<int> joints,
                                             std::vector<float> weights) {
    animation::AnimSkinStreamData data;
    data.vertexCount=mesh.getVertexCount();data.bindPositions=mesh.positions();data.bindNormals=mesh.normals();
    data.skeletonBones=std::move(bones);data.inverseBindMatrices=std::move(binds);
    for(int bone:data.skeletonBones)data.boneNames.push_back("bone-"+std::to_string(bone));
    data.vertexSkinJoints=std::move(joints);data.vertexWeights=std::move(weights);
    auto result=animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(result.ok());return std::move(result).takeValue();
}
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.remapsBoneIdentityAndBindposeExactly") {
    auto firstMesh=triangle("shared",0.F),secondMesh=triangle("shared",2.F);
    auto bindA=identityBind(),bindB=identityBind();bindB[12]=3.F;
    auto firstSkin=skinFor(firstMesh,{10,10},{bindA,bindB},
        {0,-1,-1,-1, 1,-1,-1,-1, -1,-1,-1,-1},
        {1,0,0,0, .75F,0,0,0, 0,0,0,0});
    auto secondSkin=skinFor(secondMesh,{10,20},{bindA,identityBind()},
        {0,-1,-1,-1, 1,-1,-1,-1, -1,-1,-1,-1},
        {.5F,0,0,0, 1,0,0,0, 0,0,0,0});
    procgen::PcgMeshTransform transform;
    procgen_animation::PcgSkinnedMeshPlan plan;
    REQUIRE(plan.appendSource(firstMesh,*firstSkin,transform,"shared").ok());
    REQUIRE(plan.appendSource(secondMesh,*secondSkin,transform,"shared").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output,plan).ok());
    REQUIRE(output.valid());REQUIRE(output.mesh());REQUIRE(output.skin());
    CHECK_EQ(output.mesh()->getVertexCount(),6);CHECK_EQ(output.mesh()->getIndexCount(),6);
    CHECK_EQ(output.skin()->getVertexCount(),6);CHECK_EQ(output.skin()->getBoneCount(),4);
    CHECK_EQ(output.skin()->getVertexSkinJoint(0,0),0);
    CHECK_EQ(output.skin()->getVertexSkinJoint(1,0),2);
    CHECK_EQ(output.skin()->getVertexSkinJoint(3,0),0);
    CHECK_EQ(output.skin()->getVertexSkinJoint(4,0),3);
    CHECK_EQ(output.skin()->getVertexWeight(1,0),.75F);
    CHECK_EQ(output.skin()->getInverseBindElement(2,12),3.F);
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.validationAndPublishAreAtomic") {
    animation::AnimSkinStreamData invalid;
    invalid.vertexCount=1;
    CHECK(!animation::AnimSkin::fromStreams(std::move(invalid)).ok());
    auto mesh=triangle("kept",0.F);auto bind=identityBind();
    auto skin=skinFor(mesh,{1},{bind},{0,-1,-1,-1, 0,-1,-1,-1, 0,-1,-1,-1},
                      {1,0,0,0, 1,0,0,0, 1,0,0,0});
    procgen_animation::PcgSkinnedMeshPlan validPlan;
    procgen::PcgMeshTransform transform;
    REQUIRE(validPlan.appendSource(mesh,*skin,transform,"kept").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output,validPlan).ok());
    auto* beforeSkin=output.skin();const int beforeVertices=output.mesh()->getVertexCount();
    procgen_animation::PcgSkinnedMeshPlan empty;
    CHECK(!combinePcgSkinnedMeshesInto(output,empty).ok());
    CHECK_EQ(output.skin(),beforeSkin);CHECK_EQ(output.mesh()->getVertexCount(),beforeVertices);
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.realVmCombinesOwnedResult") {
    auto mesh=triangle("skin",0.F);auto bind=identityBind();
    auto skin=skinFor(mesh,{7},{bind},{0,-1,-1,-1, 0,-1,-1,-1, 0,-1,-1,-1},
                      {1,0,0,0, 1,0,0,0, 1,0,0,0});
    procgen::Procgen procgen;
    procgen_animation::ProcgenAnimation bridge;
    ssq::VM vm(1024);auto table=vm.addTable("eve");
    auto skinClass=table.addClass<animation::AnimSkin>("AnimSkin",
        std::function<animation::AnimSkin*()>([]()->animation::AnimSkin*{return nullptr;}),true);
    skinClass.addFunc("getVertexCount",&animation::AnimSkin::getVertexCount);
    procgen.expose(table);bridge.expose(table);
    vm.addFunc("pcgSkinMesh",[&](){return &mesh;});
    vm.addFunc("pcgSkin",[&](){return skin.get();});
    vm.run(vm.compileSource(R"(
        local transform=eve.PcgMeshTransform();
        local plan=eve.PcgSkinnedMeshPlan();
        assert(plan.appendSource(pcgSkinMesh(),pcgSkin(),transform,"skin").ok);
        local output=eve.PcgSkinnedMeshResult();
        local module=eve.ProcgenAnimation();
        assert(module.combinePcgSkinnedMeshes(output,plan).ok);
        assert(output.isValid() && output.getSkin().getVertexCount()==3);
        local combined=output.copyMesh();
        assert(combined.getVertexCount()==3 && combined.getIndexCount()==3);
    )"));
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.realGraphicsAcceptsCombinedSkinStreams") {
    auto mesh=triangle("skin",0.F);auto bind=identityBind();
    auto skin=skinFor(mesh,{3},{bind},{0,-1,-1,-1, 0,-1,-1,-1, 0,-1,-1,-1},
                      {1,0,0,0, 1,0,0,0, 1,0,0,0});
    procgen_animation::PcgSkinnedMeshPlan plan;procgen::PcgMeshTransform transform;
    REQUIRE(plan.appendSource(mesh,*skin,transform,"skin").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output,plan).ok());
    GfxFixture fixture(96,96,true);procgen::Procgen procgen;
    auto gpuMesh=procgen.uploadMeshBorrowed(*output.mesh(),*fixture.gfx);
    REQUIRE(gpuMesh.isBound());
    REQUIRE(output.skin()->bindGpuMesh(fixture.gfx,gpuMesh.get()));
}
