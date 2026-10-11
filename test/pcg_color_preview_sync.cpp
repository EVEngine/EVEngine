#include "zeroerr/unittest.h"
#include "ui/PcgColorPreviewSync.h"
#include <limits>
using eve::ui::PcgColorPreviewSync;
TEST_CASE("ui.pcgColorPreview.multipliesNonBlackSource") {
 PcgColorPreviewSync sync; REQUIRE(sync.sync(.2f,.3f,.4f,.5f).ok()); CHECK_EQ(sync.getRed(),.5f); CHECK_EQ(sync.getGreen(),.75f); CHECK_EQ(sync.getBlue(),1.f); CHECK_EQ(sync.getAlpha(),1.25f);
}
TEST_CASE("ui.pcgColorPreview.blackUsesReadableGrayAndRejectsInvalid") {
 PcgColorPreviewSync sync; REQUIRE(sync.sync(0,0,0,.2f).ok()); CHECK_EQ(sync.getRed(),.5f); CHECK_EQ(sync.getGreen(),.5f); CHECK_EQ(sync.getBlue(),.5f); CHECK_EQ(sync.getAlpha(),1.f);
 CHECK(!sync.sync(std::numeric_limits<float>::quiet_NaN(),0,0,1).ok()); CHECK_EQ(sync.getRed(),.5f);
}
