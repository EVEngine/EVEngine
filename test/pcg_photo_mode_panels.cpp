#include "zeroerr/unittest.h"
#include "ui/PcgPhotoModePanels.h"
#include <limits>
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModePanels.selectsExactlyOneAndResetsScroll") { PcgPhotoModePanels p; REQUIRE(p.select(4).ok()); CHECK_EQ(p.getSelected(),4); for(int i=0;i<7;++i)CHECK_EQ(p.isActive(i),i==4); CHECK_EQ(p.consumeScrollReset(),PcgUiRequestStatus::Requested); CHECK_EQ(p.consumeScrollReset(),PcgUiRequestStatus::None); CHECK_EQ(p.getRevision(),uint64_t(1)); REQUIRE(p.select(6).ok()); CHECK(p.isActive(6)); }
TEST_CASE("ui.pcgPhotoModePanels.invalidSelectionIsAtomic") { PcgPhotoModePanels p; REQUIRE(p.select(2).ok()); auto rev=p.getRevision(); CHECK(!p.select(7).ok()); CHECK_EQ(p.getSelected(),2); CHECK_EQ(p.getRevision(),rev); CHECK(p.isActive(2)); }
TEST_CASE("ui.pcgPhotoModePanelButton.matchesColorsAndLayout") { PcgPhotoModePanelButton b; REQUIRE(b.configure(.1f,.2f,.3f,.4f,.8f,.7f,.6f,.5f).ok()); b.setSelected(false); CHECK_EQ(b.getNormalR(),.1f); CHECK_EQ(b.getHighlightR(),.8f); CHECK(!b.getLayoutEnabled()); b.setSelected(true); CHECK_EQ(b.getNormalR(),.8f); CHECK_EQ(b.getNormalA(),.5f); CHECK(b.getLayoutEnabled()); CHECK(!b.configure(std::numeric_limits<float>::quiet_NaN(),0,0,1,1,1,0,1).ok()); CHECK_EQ(b.getNormalR(),.8f); }
