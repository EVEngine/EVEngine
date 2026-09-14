#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

namespace {

UIHost *createHost(const std::string &name) {
    auto host = UIHost::resolve(UIHost::createHost(name));
    return host ? &host->get() : nullptr;
}

UINode *node(UIHost &host, const std::string &id) {
    auto found = host.findById(id);
    return found ? &found->get() : nullptr;
}

}  // namespace

TEST_CASE("ui.color_palette.flattens_rgba_and_type") {
    UIHost *host = createHost("color-palette");
    REQUIRE(host != nullptr);

    WidgetDesc palette = colorPalette("Accent", 0.1f, 0.2f, 0.3f, 0.4f, "accent");
    host->setTree(window("Palette", {std::move(palette)}, "root"));

    UINode *accent = node(*host, "accent");
    REQUIRE(accent != nullptr);
    CHECK_EQ(static_cast<int>(accent->type), static_cast<int>(NodeType::ColorPalette));
    CHECK_EQ(accent->text, std::string("Accent"));
    CHECK_EQ(accent->tintR, 0.1f);
    CHECK_EQ(accent->tintG, 0.2f);
    CHECK_EQ(accent->tintB, 0.3f);
    CHECK_EQ(accent->tintA, 0.4f);
}

TEST_CASE("ui.color_palette.builder_and_script_accessors") {
    UI *uimod = UI::create();
    REQUIRE(uimod != nullptr);
    uimod->beginBuild();
    uimod->beginWindow("Colors", "root");
    uimod->addColorPalette("Fill", 0.8f, 0.1f, 0.2f, 1.f, "fill");
    uimod->end();
    CHECK(uimod->mountBuildAs("palette-host"));
    CHECK(uimod->select("palette-host"));
    CHECK_EQ(uimod->getColorR("fill"), 0.8f);
    CHECK_EQ(uimod->getColorG("fill"), 0.1f);
    CHECK_EQ(uimod->getColorB("fill"), 0.2f);
    CHECK_EQ(uimod->getColorA("fill"), 1.f);
    uimod->setColor("fill", 0.0f, 1.0f, 0.5f, 0.25f);
    CHECK_EQ(uimod->getColorR("fill"), 0.0f);
    CHECK_EQ(uimod->getColorG("fill"), 1.0f);
    CHECK_EQ(uimod->getColorB("fill"), 0.5f);
    CHECK_EQ(uimod->getColorA("fill"), 0.25f);
}
