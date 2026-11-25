// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"stylize","default":"script-binding"}
#include <algorithm>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "image/Image.h"
#include "image/ImageData.h"
#include "stylize/Stylize.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_act_script {
TEST_CASE("stylize.pixelfx.actScript.preparedReplayReplacesIndependentOwner") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    vm.run(vm.compileSource("eve <- {};"));
    ssq::Table table(vm.find("eve"));
    eve::stylize::Stylize::expose(table);
    vm.run(vm.compileSource(R"sq(
        function need(r){if(!r.ok)throw "replay Result failed";return r.value;}
        function fresh(){return need(need(eve.PixelFxActSession().prepare(eve.PixelFx("moves/fireball-throw"))).publish());}
        local origin=fresh(),expected=fresh(),history=[{op="step",delta=0}];
        for(local i=0;i<240;i++){
            history.push({op="step",delta=0.01});need(expected.step(0.01));
            if(i==120){history.push({op="stop"});expected.stop();}
        }
        need(origin.step(0.3));origin.stop();local before=need(origin.getSnapshot());
        foreach(invalid in [null,{},[null],[3],[{}],[{op="step"}],[{op="step",delta=true}],
                            [{op="step",delta=0,unknown=1}],[{op="stop",delta=0}],[{op="other"}],
                            [{op="step",delta=0.1},{op="step",delta=-1}]]){
            if(origin.prepareReplay(invalid).ok)throw "bad replay accepted";
            if(need(origin.getSnapshot()).wallTime!=before.wallTime)throw "bad replay mutated owner";
        }
        local prepared=need(origin.prepareReplay(history));
        if(need(origin.getSnapshot()).wallTime!=before.wallTime)throw "preparation advanced owner";
        origin=null;history=null;collectgarbage();
        local replacement=need(prepared.publish());
        if(prepared.publish().ok)throw "replay token reused";
        local e=need(expected.getSnapshot()),r=need(replacement.getSnapshot());
        if(e.wallTime!=r.wallTime||e.actTime!=r.actTime||e.effectTime!=r.effectTime||e.stopped!=r.stopped||
           e.freezeRemaining!=r.freezeRemaining||e.hits.len()!=r.hits.len()||e.presence.len()!=r.presence.len())
            throw "replay state differs from original command order";
        local reset=need(need(replacement.prepareReplay([])).publish());
        if(need(reset.getSnapshot()).wallTime!=0||need(reset.getSnapshot()).stopped)throw "empty replay did not reset";
        replayPassed <- true;
    )sq"));
    REQUIRE(vm.find("replayPassed").toBool());
}
TEST_CASE("stylize.pixelfx.actScript.synchronousHitObserver") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    vm.run(vm.compileSource("eve <- {};"));
    ssq::Table table(vm.find("eve"));
    eve::stylize::Stylize::expose(table);
    vm.run(vm.compileSource(R"sq(
        function need(r){if(!r.ok)throw "observer Result failed";return r.value;}
        function fresh(){return need(need(eve.PixelFxActSession().prepare(eve.PixelFx("moves/arrow-shot"))).publish());}
        local act=fresh(),count=0,saved=null;
        local initial=need(act.getSnapshot());
        if(act.stepObserved(0.1,null).ok||act.stepObserved("bad",function(h,e){}).ok)throw "bad observer accepted";
        if(need(act.getSnapshot()).wallTime!=initial.wallTime)throw "invalid observer mutated clocks";
        local observer=function(h,e){
            count++;saved={hit=h,event=e};
            local s=need(act.getSnapshot());
            if(!s.frozen||s.hits.len()!=1||e.index!=0||h.t!=e.sourceTime||s.actTime!=e.actTime)throw "wrong callback phase";
            if(act.step(0.0).ok||act.stepObserved(0.0,function(a,b){}).ok)throw "recursive update accepted";
            if(act.prepareReplay([]).ok)throw "observer prepared recursive replay";
            act.stop();
        };
        for(local i=0;i<240&&count==0;i++)need(act.stepObserved(1.0/120.0,observer));
        if(count!=1||!need(act.getSnapshot()).stopped||saved.hit.t<=0)throw "observer did not stop at hit";
        observer=null;act=null;collectgarbage();
        if(saved.event.index!=0||saved.hit.t!=saved.event.sourceTime)throw "callback payload lifetime lost";
        local retry=fresh(),attempts=0,failed=false;
        for(local i=0;i<240&&!failed;i++){
            local r=retry.stepObserved(1.0/120.0,function(h,e){attempts++;throw "expected hit failure";});
            if(!r.ok)failed=true;
        }
        if(!failed||attempts!=1)throw "callback exception was swallowed";
        local recovered=0;
        for(local i=0;i<240&&recovered==0;i++)need(retry.stepObserved(1.0/120.0,function(h,e){recovered++;if(e.index!=0)throw "throwing hit was consumed";}));
        if(recovered!=1)throw "callback retry failed";
        need(retry.step(0.0));
        observerPassed <- true;
    )sq"));
    REQUIRE(vm.find("observerPassed").toBool());
}
TEST_CASE("stylize.pixelfx.actScript.ownedSessionRuntimeAndFrame") {
    auto*                                  module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> color(module->newImageData(96, 96)), light(module->newImageData(96, 96)),
        wrong(module->newImageData(16, 16));
    ssq::VM vm(2048, ssq::Libs::ALL);
    vm.run(vm.compileSource("eve <- {};"));
    ssq::Table table(vm.find("eve"));
    eve::image::Image::expose(table);
    eve::stylize::Stylize::expose(table);
    table.addFunc("testColor", [&]() { return color.get(); });
    table.addFunc("testLight", [&]() { return light.get(); });
    table.addFunc("testWrong", [&]() { return wrong.get(); });
    vm.run(vm.compileSource(R"sq(
        function need(result) { if(!result.ok) throw "Act Result failed"; return result.value; }
        local effect=eve.PixelFx("moves/arrow-shot");
        local session=eve.PixelFxActSession();
        if(session.prepare(null).ok) throw "null effect accepted";
        local candidate=need(session.prepare(effect));
        local act=need(candidate.publish());
        local timing=need(act.getTiming());
        if(timing.schema!="eve.pixelfx.act-timing"||timing.windup<=0||timing.hits.len()!=1||timing.poses.len()<2)throw "authored timing lost";
        if(need(session.getStatistics()).rasterEvaluations!=0)throw "timing query rasterized";
        savedBounds <- need(act.prepareBounds());
        local bounds=need(savedBounds.getValue());if(bounds.x1<=bounds.x0||bounds.y1<=bounds.y0)throw "bad bounds";
        if(need(session.getStatistics()).rasterEvaluations==0)throw "bounds did not use session";
        session=null;collectgarbage();
        if(candidate.publish().ok) throw "consumed candidate accepted";
        local initial=need(act.getSnapshot());
        if(initial.schema!="eve.pixelfx.act-snapshot"||initial.version!=1||initial.actTime>=0)throw "missing windup";
        if(act.step("bad").ok)throw "bad delta accepted";
        if(need(act.getSnapshot()).wallTime!=initial.wallTime)throw "rejected step changed time";
        for(local i=0;i<5;i++){local r=act.step(0.25);if(!r.ok)throw "step failed";}
        local after=need(act.getSnapshot());
        if(!after.frozen||after.hits.len()!=1)throw "source hitstop lost";
        local c=eve.testColor(),l=eve.testLight();
        if(act.prepareFrame(null,l,{}).ok)throw "null image accepted";
        if(act.prepareFrame(effect,l,{}).ok)throw "wrong image type accepted";
        if(act.prepareFrame(c,c,{}).ok)throw "aliased planes accepted";
        if(act.prepareFrame(c,l,{unknown=true}).ok)throw "unknown options accepted";
        captured <- need(act.prepareFrame(c,l,{originX=48,originY=80,body="source",targets=false}));
        if(captured.copyTo(null,l).ok)throw "null output accepted";
        if(captured.copyTo(c,eve.testWrong()).ok)throw "mismatched output accepted";
        savedSnapshot <- after;
        candidate=null;act=null;session=null;effect=null;c=null;l=null;collectgarbage();
        stageOne <- true;
    )sq"));
    REQUIRE(vm.find("stageOne").toBool());
    const auto* initial = static_cast<const uint8_t*>(color->getData());
    REQUIRE(std::all_of(initial, initial + color->getSize(), [](uint8_t c) { return c == 0; }));
    color.reset(module->newImageData(96, 96));
    light.reset(module->newImageData(96, 96));
    vm.run(vm.compileSource(R"sq(
        local result=captured.copyTo(eve.testColor(),eve.testLight());if(!result.ok)throw "orphan frame failed";
        if(savedSnapshot.id!="moves/arrow-shot"||!savedSnapshot.frozen)throw "snapshot lifetime lost";
        local bounds=need(savedBounds.getValue());if(bounds.x1<=bounds.x0)throw "bounds lifetime lost";
        local fire=eve.PixelFx("fire/campfire");
        local candidate=need(eve.PixelFxActSession().prepare(fire));local plain=need(candidate.publish());
        if(need(plain.getTiming())!=null||need(plain.getSnapshot()).hasAct)throw "missing Act not represented";
        if(plain.prepareFrame(eve.testColor(),eve.testLight(),{width=97}).ok)throw "oversized logical extent accepted";
        cropped <- need(plain.prepareFrame(eve.testColor(),eve.testLight(),{width=32,height=24,body="actor"}));
        stageTwo <- true;
    )sq"));
    REQUIRE(vm.find("stageTwo").toBool());
    const auto* pixels = static_cast<const uint8_t*>(color->getData());
    REQUIRE(std::any_of(pixels, pixels + color->getSize(), [](uint8_t c) { return c != 0; }));
    vm.run(vm.compileSource(
        R"sq(local r=cropped.copyTo(eve.testColor(),eve.testLight());if(!r.ok)throw "cropped frame failed";)sq"));
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x)
            if (y >= 24 || x >= 32) {
                const size_t offset = (size_t(y) * 96 + size_t(x)) * 4;
                for (int c = 0; c < 4; ++c) REQUIRE(pixels[offset + size_t(c)] == 0);
            }
}

TEST_CASE("stylize.pixelfx.actScript.all244MetadataAndFrames") {
    auto*                                  module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> color(module->newImageData(96, 96)), light(module->newImageData(96, 96));
    ssq::VM                                vm(2048, ssq::Libs::ALL);
    vm.run(vm.compileSource("eve <- {};"));
    ssq::Table table(vm.find("eve"));
    eve::image::Image::expose(table);
    eve::stylize::Stylize::expose(table);
    table.addFunc("testColor", [&]() { return color.get(); });
    table.addFunc("testLight", [&]() { return light.get(); });
    vm.run(vm.compileSource(R"sq(
        function need(r){if(!r.ok)throw "Act catalogue Result failed";return r.value;}
        local session=eve.PixelFxActSession(),catalogue=need(session.getCatalogue());
        if(catalogue.schema!="eve.pixelfx.catalogue"||catalogue.version!=1||catalogue.effects.len()!=244)throw "wrong catalogue";
        if(need(session.getStatistics()).rasterEvaluations!=0)throw "catalogue performed raster work";
        local authored=0,seen={};
        foreach(info in catalogue.effects){
            if(info.id in seen)throw "duplicate effect";seen[info.id]<-true;
            local fx=eve.PixelFx(info.id),candidate=need(session.prepare(fx)),act=need(candidate.publish());
            local timing=need(act.getTiming()),snapshot=need(act.getSnapshot());
            if(snapshot.id!=info.id||snapshot.hasAct!=(timing!=null))throw "metadata mismatch";
            if(snapshot.hasAct)authored++;
            local stepped=act.step(0.015625);if(!stepped.ok)throw "Act step failed";
            local frame=need(act.prepareFrame(eve.testColor(),eve.testLight(),{originX=48,originY=80,body="actor"}));
            local copied=frame.copyTo(eve.testColor(),eve.testLight());if(!copied.ok)throw "Act frame failed";
            frame=null;act=null;candidate=null;fx=null;collectgarbage();
        }
        if(authored!=73)throw "wrong authored Act count";
        if(need(session.getStatistics()).rasterEvaluations!=0)throw "input images rasterized a substitute rig";
        allPassed <- true;
    )sq"));
    REQUIRE(vm.find("allPassed").toBool());
}

TEST_CASE("stylize.pixelfx.actScript.logicalExtentUsesBackgroundStrideAndClearsPadding") {
    auto*                                  module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> sourceColor(module->newImageData(96, 96)),
        sourceLight(module->newImageData(96, 96)), outputColor(module->newImageData(96, 96)),
        outputLight(module->newImageData(96, 96));
    auto* c = static_cast<uint8_t*>(sourceColor->getData());
    auto* l = static_cast<uint8_t*>(sourceLight->getData());
    for (size_t i = 0; i < 96 * 96; ++i) {
        for (size_t ch = 0; ch < 3; ++ch) {
            c[i * 4 + ch] = uint8_t(i * 17 + ch * 37);
            l[i * 4 + ch] = uint8_t(i * 23 + ch * 19);
        }
        c[i * 4 + 3] = 255;
        l[i * 4 + 3] = 255;
    }
    ssq::VM vm(2048, ssq::Libs::ALL);
    vm.run(vm.compileSource("eve <- {};"));
    ssq::Table table(vm.find("eve"));
    eve::image::Image::expose(table);
    eve::stylize::Stylize::expose(table);
    table.addFunc("sourceColor", [&]() { return sourceColor.get(); });
    table.addFunc("sourceLight", [&]() { return sourceLight.get(); });
    table.addFunc("outputColor", [&]() { return outputColor.get(); });
    table.addFunc("outputLight", [&]() { return outputLight.get(); });
    vm.run(vm.compileSource(R"sq(
        function need(r){if(!r.ok)throw "logical frame Result failed";return r.value;}
        local fx=eve.PixelFx("fire/campfire"),session=eve.PixelFxActSession();
        local act=need(need(session.prepare(fx)).publish());
        local frame=need(act.prepareFrame(eve.sourceColor(),eve.sourceLight(),{width=31,height=23,x=400.0,y=400.0,body="none"}));
        local copied=frame.copyTo(eve.outputColor(),eve.outputLight());if(!copied.ok)throw "logical frame copy failed";
    )sq"));
    const auto* outC = static_cast<const uint8_t*>(outputColor->getData());
    const auto* outL = static_cast<const uint8_t*>(outputLight->getData());
    for (size_t y = 0; y < 96; ++y)
        for (size_t x = 0; x < 96; ++x)
            for (size_t ch = 0; ch < 4; ++ch) {
                const size_t i = (y * 96 + x) * 4 + ch;
                REQUIRE(outC[i] == (x < 31 && y < 23 ? c[i] : uint8_t(0)));
                REQUIRE(outL[i] == (x < 31 && y < 23 ? l[i] : uint8_t(0)));
            }
}
}  // namespace test_pixelfx_act_script
