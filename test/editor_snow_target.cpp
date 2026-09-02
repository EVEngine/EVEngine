#include "editor/EditCommand.h"
#include "level_editing/FieldTargets.h"
#include "snow_editor/SnowFieldTarget.h"

#include "snow/SnowField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;
using namespace eve::level_editing;

TEST_CASE("editor.snow.live_scalar_target_supports_bilinear_preview_and_reversible_brush_edits") {
    eve::snow::SnowField field(2, 2);
    field.fill(1.0F);
    SnowFieldTarget        target("snow-depth", &field);
    ScalarFieldEditCommand command("footprint", &target);
    REQUIRE(command.record(0, 0, 0.0F));
    REQUIRE(command.record(1, 0, 0.5F));
    REQUIRE(command.apply());
    CHECK_EQ(field.height(0, 0), 0.0F);
    CHECK_EQ(target.sampleScalar(0.5F, 0.0F), 0.25F);
    CHECK(!target.dirtyRegion().empty());
    command.revert();
    CHECK_EQ(field.height(0, 0), 1.0F);
    CHECK_EQ(field.height(1, 0), 1.0F);
}
