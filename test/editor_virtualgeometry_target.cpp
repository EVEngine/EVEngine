#include "editor/EditorVirtualGeometryTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
SelectionSnapshot select(const VirtualGeometryDocumentTarget& target){SelectionSnapshot s;s.channel="virtualgeometry";s.items.push_back({SelectionDomain::Asset,TargetId(target.targetId()),StableId(target.targetId()),"virtualgeometry.import"});return s;}
void apply(VirtualGeometryDocumentTarget& target,EditorResult<DomainOperation> operation){REQUIRE(operation.value);REQUIRE(target.applyDomainOperation(*operation.value).accepted());}
class GridResolver final : public IVirtualGeometryMeshResolver {
public:
    EditorResult<VirtualGeometryMeshData> resolve(const std::string& id) const override {
        if(id!="grid")return EditorResult<VirtualGeometryMeshData>::error(EditorStatus::NotFound,RuleId("test.mesh"),"missing mesh");
        VirtualGeometryMeshData mesh;constexpr int size=20;for(int y=0;y<=size;++y)for(int x=0;x<=size;++x)mesh.positions.insert(mesh.positions.end(),{static_cast<float>(x),0.f,static_cast<float>(y)});for(int y=0;y<size;++y)for(int x=0;x<size;++x){const std::uint32_t a=y*(size+1)+x,b=a+1,c=a+size+1,d=c+1;mesh.indices.insert(mesh.indices.end(),{a,c,b,b,c,d});}return EditorResult<VirtualGeometryMeshData>::applied(std::move(mesh));
    }
};
}

TEST_CASE("editor.virtualgeometry.import_preset_is_reversible_and_atomic") {
    VirtualGeometryDocumentTarget target("vg");const auto selection=select(target);
    auto edit=target.makeSet(selection,PropertyPath("cluster.maxTriangles"),int64_t{64},PropertySetMode::Absolute);REQUIRE(edit.value);REQUIRE(target.applyDomainOperation(*edit.value).accepted());CHECK_EQ(target.value().builder.maxTrianglesPerCluster,64);
    DomainOperation undo=*edit.value;undo.payload=edit.value->inverse;REQUIRE(target.applyDomainOperation(undo).accepted());CHECK_EQ(target.value().builder.maxTrianglesPerCluster,124);
    const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();(*content)["mergeFactor"]=int64_t{20};CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);
}

TEST_CASE("editor.virtualgeometry.build_is_deterministic_and_emits_lod_cost_curve") {
    VirtualGeometryDocumentTarget target("vg");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source.asset"),"grid",PropertySetMode::Absolute));apply(target,target.makeSet(selection,PropertyPath("preview.samples"),int64_t{12},PropertySetMode::Absolute));
    GridResolver resolver;VirtualGeometryBuildRuntime runtime;auto first=runtime.build(target,resolver);REQUIRE(first.value);CHECK(first.value->clusters>0);CHECK(first.value->maxLod>0);CHECK(first.value->roots>0);CHECK_EQ(first.value->lodCurve.size(),static_cast<std::size_t>(12));CHECK(first.value->lodCurve.front().distance<first.value->lodCurve.back().distance);CHECK(first.value->lodCurve.front().triangles>=first.value->lodCurve.back().triangles);
    const auto checksum=first.value->checksum;auto second=runtime.build(target,resolver);REQUIRE(second.value);CHECK_EQ(second.value->checksum,checksum);
}

TEST_CASE("editor.virtualgeometry.failed_rebuild_preserves_previous_generation") {
    VirtualGeometryDocumentTarget target("vg");const auto selection=select(target);apply(target,target.makeSet(selection,PropertyPath("source.asset"),"grid",PropertySetMode::Absolute));GridResolver resolver;VirtualGeometryBuildRuntime runtime;REQUIRE(runtime.build(target,resolver).accepted());const auto*asset=runtime.asset();const auto revision=runtime.revision();apply(target,target.makeSet(selection,PropertyPath("source.asset"),"missing",PropertySetMode::Absolute));CHECK_EQ(static_cast<int>(runtime.build(target,resolver).status),static_cast<int>(EditorStatus::NotFound));CHECK(runtime.asset()==asset);CHECK_EQ(runtime.revision(),revision);
}
