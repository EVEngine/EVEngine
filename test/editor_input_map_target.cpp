#include "editor/EditorInputMapTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::editor;
namespace{void apply(InputMapTarget&t,EditorResult<DomainOperation>op){REQUIRE(op.value);REQUIRE(t.applyDomainOperation(*op.value).accepted());}}
TEST_CASE("editor.input_map.evaluates_composite_axis_deadzone_and_buttons"){
 InputMapTarget target("game.input");apply(target,target.makeCreateAction({ObjectId("move"),"move-x","axis1d"}));apply(target,target.makeCreateAction({ObjectId("jump"),"jump","button"}));apply(target,target.makeCreateBinding({ObjectId("left"),ObjectId("move"),"keyboard","A",-1,0,false}));apply(target,target.makeCreateBinding({ObjectId("right"),ObjectId("move"),"keyboard","D",1,0,false}));apply(target,target.makeCreateBinding({ObjectId("stick"),ObjectId("move"),"gamepad","leftx",1,.2,false}));apply(target,target.makeCreateBinding({ObjectId("space"),ObjectId("jump"),"keyboard","Space",1,0,false}));InputMapEvaluator evaluator;auto result=evaluator.evaluate(target,{{"keyboard","D",1},{"gamepad","leftx",.6},{"keyboard","Space",1}});REQUIRE(result.value);CHECK_EQ(result.value->at("move-x"),1.0);CHECK_EQ(result.value->at("jump"),1.0);
}
TEST_CASE("editor.input_map.rejects_dangling_delete_and_captures_intentional_input"){
 InputMapTarget target("game.input");apply(target,target.makeCreateAction({ObjectId("jump"),"jump","button"}));apply(target,target.makeCreateBinding({ObjectId("space"),ObjectId("jump"),"keyboard","Space",1,0,false}));CHECK_EQ(static_cast<int>(target.makeDeleteAction(ObjectId("jump")).status),static_cast<int>(EditorStatus::Rejected));InputBindingCapture capture;capture.begin("gamepad");CHECK(!capture.feed({"keyboard","A",1}));CHECK(!capture.feed({"gamepad","leftx",.2}));auto captured=capture.feed({"gamepad","leftx",.8});REQUIRE(captured);CHECK_EQ(captured->control,std::string("leftx"));CHECK(!capture.active());
}
TEST_CASE("editor.input_map.snapshot_load_is_atomic"){
 InputMapTarget target("game.input");apply(target,target.makeCreateAction({ObjectId("jump"),"jump","button"}));const auto before=target.snapshotValue();EditorValue invalid=before;auto*root=invalid.getIf<EditorValue::Object>();auto*content=(*root)["content"].getIf<EditorValue::Object>();auto*actions=(*content)["actions"].getIf<EditorValue::Array>();auto*first=(*actions)[0].getIf<EditorValue::Object>();(*first)["kind"]="vector3";CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),static_cast<int>(EditorStatus::Rejected));CHECK_EQ(target.snapshotValue(),before);
}
