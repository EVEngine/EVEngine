#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.vegetation.script_owned_field_roundtrip") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local creation = eve.Graphics().newVegetationField();
        assert(creation.ok && creation.ownership == "owned");
        local field = creation.value;
        local saved = field.snapshot();
        assert(saved.ok && saved.value.schema == "eve.graphics.vegetation-field");
        saved.value.globals.motion[2] = 0.8;
        assert(field.restore(saved.value).ok);
        local sampled = field.sample(0.0,0.0,0.0,0,0,0,0);
        assert(sampled.ok && sampled.value.motion[2] > 0.79);
        assert(!field.sample(0.0,0.0,0.0,256,0,0,0).ok);
        saved.value.version = 2;
        assert(!field.restore(saved.value).ok);
        assert(field.sample(0.0,0.0,0.0,0,0,0,0).value.motion[2] > 0.79);

        local detailsCreation = eve.Graphics().newVegetationDetails();
        assert(detailsCreation.ok && detailsCreation.ownership == "owned");
        local details = detailsCreation.value;
        local detailDocument = details.snapshot();
        assert(detailDocument.ok && detailDocument.value.schema == "eve.graphics.vegetation-details");
        detailDocument.value.layers = [2, 3, 4];
        detailDocument.value.global = [0.6, 0.7, 0.8, 0.9];
        assert(details.restore(detailDocument.value).ok);
        local after = details.snapshot();
        assert(after.ok && after.value.layers[2] == 4 && after.value.global[1] > 0.69);
        detailDocument.value.version = 2;
        assert(!details.restore(detailDocument.value).ok);
        assert(details.snapshot().value.layers[2] == 4);
    )"));
}
