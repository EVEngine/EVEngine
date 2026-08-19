#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UIHost.h"
#include "ui/Widget.h"

// Regression probe: WidgetDesc move must work in the test binary even with the
// image/layout fields present (crash investigation, kept as a regression test).
TEST_CASE("UI.regression.widgetDescMove") {
    eve::ui::WidgetDesc w;
    w.id = "root";
    eve::ui::WidgetDesc moved = std::move(w);
    CHECK(moved.id == "root");
}
