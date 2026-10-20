#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "graphics/material/editor/MaterialEditor.h"
#include "scripts.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
void loadMaterialStudio(ssq::VM& vm) {
    const std::string embedded(eve::load_content);
    const auto        begin = embedded.find("class MaterialPreviewGeometry {");
    REQUIRE_NE(begin, std::string::npos);
    const auto factory = embedded.find("function createMaterialStudio(", begin);
    REQUIRE_NE(factory, std::string::npos);
    const auto end = embedded.find("\n}", factory);
    REQUIRE_NE(end, std::string::npos);
    const auto source = embedded.substr(begin, end + 2 - begin);
    vm.run(vm.compileSource(source.c_str()));
}
}  // namespace

TEST_CASE("editor.material.facade_script_projects_owned_values_and_checked_failures") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function checked(result) {
            if (!result.ok) throw result.status.summary;
            return result.value;
        }
        local editorHost = eve.Editor();
        local module = eve.MaterialEditorModule();
        local created = module.create("test.material");
        assert(created.ok && created.ownership == "owned");
        local document = created.value;
        assert(!module.create("").ok);
        local original = checked(document.snapshot());
        checked(document.patch({["shading.metallic"]=1.0, ["shading.roughness"]=0.2}));
        assert(checked(document.read("shading.metallic")) == 1.0);
        checked(document.undo());
        assert(checked(document.read("shading.metallic")) == 0.0);
        assert(document.canRedo());
        assert(!document.patch({["shading.metallic"]=-1.0}).ok);
        assert(!document.patch([1,2,3]).ok);
        assert(!document.set("shading.metallic", function() {}).ok);
        local cyclic = {}; cyclic.self <- cyclic;
        assert(!document.patch(cyclic).ok);
        assert(!document.read("missing").ok);
        assert(!document.fromJson("invalid json").ok);
        assert(document.canRedo());
        checked(document.redo());
        local json = checked(document.toJson());
        local copy = checked(module.create("copy"));
        checked(copy.fromJson(json));
        assert(checked(copy.read("shading.metallic")) == 1.0);
        assert(checked(copy.schema()).properties.len() > 50);
        checked(copy.restore(original));
        assert(checked(copy.read("shading.metallic")) == 0.0);
    )"));
}

TEST_CASE("editor.material.studio_publication_clears_resources_and_preserves_preview_on_failure") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    loadMaterialStudio(vm);
    vm.run(vm.compileSource(R"(
        class RecordingMaterial {
            values = null;
            constructor() { values = {}; }
            function _get(key) {
                return function(...) {
                    foreach (value in vargv) ::assert(value != null, "Null resource reached a typed binding");
                    values[key] <- vargv;
                }.bindenv(this);
            }
        }
        local editorHost = eve.Editor();
        gfx <- {newMaterial=function() { return RecordingMaterial(); }};
        ui <- {select=function(...) {}, setValue=function(...) {},
               setColor=function(...) {}, setChecked=function(...) {}};
        local studio = MaterialStudio();
        studio.matEd.document = studio.checked(eve.MaterialEditorModule().create("studio.publication"));
        studio.syncFromDocument();
        studio.matEd.material = RecordingMaterial();
        studio.matEd.ball = RecordingMaterial();
        studio.applyMaterial(false);
        assert(studio.previewError == "", studio.previewError);
        local original = studio.getMaterial();
        studio.options.resolveTexture <- function(asset) { return {asset=asset}; };
        studio.checked(studio.getDocument().set("textures.albedo", "asset://albedo"));
        studio.applyMaterial(false);
        assert(studio.previewError == "", studio.previewError);
        assert(studio.getMaterial().values.setAlbedoTexture[0].asset == "asset://albedo");
        assert(studio.getMaterial() != original);
        studio.checked(studio.getDocument().set("textures.albedo", ""));
        studio.applyMaterial(false);
        assert(studio.previewError == "", studio.previewError);
        assert(!("setAlbedoTexture" in studio.getMaterial().values));
        local beforeFailure = studio.getMaterial();
        studio.options.resolveTexture = function(asset) { throw "injected resource failure"; };
        studio.checked(studio.getDocument().set("textures.albedo", "asset://unresolved"));
        studio.applyMaterial(false);
        assert(studio.previewError == "injected resource failure");
        assert(studio.getMaterial() == beforeFailure);
        studio.checked(studio.getDocument().set("textures.albedo", ""));
        studio.syncFromDocument();
        studio.matEd.metallic = 0.9;
        studio.matEd.pendingEdit = true;
        studio.checked(studio.getDocument().set("shading.metallic", 0.3));
        studio.applyMaterial();
        assert(studio.checked(studio.getDocument().read("shading.metallic")) == 0.3);
        assert(!studio.matEd.pendingEdit);
    )"));
}


TEST_CASE("editor.material.preview_geometry_has_recessed_rings_valid_normals_and_matching_sky_edges") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    loadMaterialStudio(vm);
    vm.run(vm.compileSource(R"(
        foreach (kind in ["shaderball","torus"]) {
            local data=MaterialPreviewGeometry.build(kind,32);
            assert(data.positions.len()==data.normals.len());
            assert(data.uvs.len()*3==data.positions.len()*2);
            foreach (index in data.indices) assert(index>=0 && index<data.positions.len()/3);
            for (local i=0;i<data.normals.len();i+=3) {
                local x=data.normals[i], y=data.normals[i+1], z=data.normals[i+2];
                assert(fabs(x*x+y*y+z*z-1.0)<0.0001);
            }
            if (kind=="shaderball") foreach (height in [0.18,-0.46]) {
                local row=((height+1.0)*192/1.82).tointeger(), at=row*33*3;
                local x=data.positions[at], y=data.positions[at+1]-0.10;
                local originalY=-sin(0.42)*x+cos(0.42)*y;
                local radius=cos(0.42)*x+sin(0.42)*y;
                assert(radius < sqrt(1.0-originalY*originalY)-0.04);
            }
        }
        foreach (kind in ["studio","daylight","sunset"]) {
            for (local face=0;face<6;++face) {
                local color=MaterialPreviewGeometry.skyColor(kind,MaterialPreviewGeometry.faceDirection(face,0.3,-0.5));
                foreach (value in color) assert(value>=0.0 && value<=1.0);
            }
            local left=MaterialPreviewGeometry.skyColor(kind,MaterialPreviewGeometry.faceDirection(0,-1.0,0.3));
            local right=MaterialPreviewGeometry.skyColor(kind,MaterialPreviewGeometry.faceDirection(4,1.0,0.3));
            for (local i=0;i<3;++i) assert(fabs(left[i]-right[i])<0.0001);
        }
        local rejected=false;
        try { MaterialPreviewGeometry.build("shaderball",0); } catch(error) { rejected=true; }
        assert(rejected);
    )"));
}

TEST_CASE("editor.material.preview_selection_failures_preserve_active_resources_and_fit_shared_bounds") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    loadMaterialStudio(vm);
    vm.run(vm.compileSource(R"(
        ensure_module <- function(slot) { return false; };
        local scene=MaterialPreviewScene();
        scene.activeModel="sphere";
        local original=scene.modelEntities;
        scene.registerModel("broken","Broken",function() { throw "injected decode failure"; });
        local rejected=false;
        try { scene.selectModel("broken",null,0.0); } catch(error) { rejected=true; }
        assert(rejected && scene.activeModel=="sphere" && scene.modelEntities==original);
        rejected=false;
        try { scene.importMeshes("model.glb"); } catch(error) {
            rejected=error.find("requires model3d")!=null;
        }
        assert(rejected);
        scene.registerModel("empty","Empty",function() { return []; });
        rejected=false;
        try { scene.selectModel("empty",null,0.0); } catch(error) { rejected=true; }
        assert(rejected && scene.activeModel=="sphere");
        local a={getLocalBounds=function() { return {ok=true,value=[100,50,-60,102,52,-58]}; }};
        local b={getLocalBounds=function() { return {ok=true,value=[104,49,-62,106,53,-60]}; }};
        local fitted=scene.boundsFor([a,b]);
        assert(fitted.center[0]==103.0 && fitted.center[1]==51.0 && fitted.center[2]== -60.0);
        assert(fabs(fitted.scale*6.0-2.6)<0.00001);
        local studio=MaterialStudio({resolveTexture=function(path) { throw "injected sky failure"; }});
        studio.previewScene.activeEnvironment="studio";
        local count=studio.previewScene.environments.len(), sky=studio.previewScene.skyEntities;
        rejected=false;
        try { studio.loadSkybox("broken"); } catch(error) { rejected=true; }
        assert(rejected && studio.previewScene.environments.len()==count);
        assert(studio.previewScene.activeEnvironment=="studio" && studio.previewScene.skyEntities==sky);
    )"));
}
