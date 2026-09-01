#include "voxel_editing/VoxelPaletteTarget.h"
#include "voxel/CubeTypeRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::voxel_editing;
using namespace eve::editing;
namespace{
void apply(VoxelPaletteTarget&t,EditorResult<DomainOperation>op){REQUIRE(op.ok());REQUIRE(t.applyDomainOperation(op.value()).ok());}
VoxelPaletteEntryValue cube(const std::string&id,bool directional=false){VoxelPaletteEntryValue v;v.id=ObjectId("editor-"+id);v.type.name=id;v.type.directional=directional;for(int i=0;i<6;++i)v.type.faceTex[i]=static_cast<std::uint8_t>(i+1);return v;}
SelectionSnapshot select(const VoxelPaletteTarget&t,const char*id){SelectionSnapshot s;s.channel="voxel-palette";s.items.push_back({SelectionDomain::Asset,TargetId(t.targetId()),StableId(id),"voxel.cube-type"});return s;}
}
TEST_CASE("editor.voxel_palette.face_inspector_is_reversible_and_persistent"){
 VoxelPaletteTarget target("blocks");apply(target,target.makeCreate(cube("furnace",true)));const auto selection=select(target,"editor-furnace");EditorValue::Array faces{int64_t{9},int64_t{8},int64_t{7},int64_t{6},int64_t{5},int64_t{4}};auto edit=target.makeSet(selection,PropertyPath("cube.faces"),faces,PropertySetMode::Absolute);REQUIRE(edit.ok());REQUIRE(target.applyDomainOperation(edit.value()).ok());CHECK_EQ(int(target.entries().front().type.faceTex[0]),9);DomainOperation undo=edit.value();undo.payload=edit.value().inverse;REQUIRE(target.applyDomainOperation(undo).ok());CHECK_EQ(int(target.entries().front().type.faceTex[0]),1);VoxelPaletteTarget restored("copy");REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());CHECK(restored.entries().front().type.directional);
}
TEST_CASE("editor.voxel_palette.enforces_variant_capacity_before_runtime"){
 VoxelPaletteTarget target("blocks");for(int i=0;i<63;++i)apply(target,target.makeCreate(cube("directional-"+std::to_string(i),true)));CHECK_EQ(static_cast<int>(target.makeCreate(cube("overflow",true)).code()),static_cast<int>(EditorStatus::Rejected));apply(target,target.makeCreate(cube("single",false)));CHECK_EQ(target.entries().size(),static_cast<std::size_t>(64));
}
TEST_CASE("editor.voxel_palette.publication_is_atomic_and_reports_variant_ids"){
 VoxelPaletteTarget target("blocks");apply(target,target.makeCreate(cube("stone")));apply(target,target.makeCreate(cube("furnace",true)));VoxelPaletteRuntime runtime;auto result=runtime.publish(target);REQUIRE(result.ok());CHECK_EQ(result.value().size(),static_cast<std::size_t>(2));CHECK_EQ(result.value()[0].baseId,1);CHECK_EQ(result.value()[1].baseId,2);CHECK_EQ(result.value()[1].variants,4);REQUIRE(runtime.registry());CHECK_EQ(int(runtime.registry()->variantId("furnace",3)),5);CHECK(runtime.registry()->find("furnace")!=nullptr);
}
TEST_CASE("editor.voxel_palette.invalid_snapshot_does_not_partially_replace"){
 VoxelPaletteTarget target("blocks");apply(target,target.makeCreate(cube("stone")));const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();auto*entries=(*content)["entries"].getIf<EditorValue::Array>();auto*entry=(*entries)[0].getIf<EditorValue::Object>();auto*faces=(*entry)["faces"].getIf<EditorValue::Array>();(*faces)[0]=int64_t{999};CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);
}
