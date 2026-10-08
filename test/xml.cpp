#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Xml.h"

using namespace eve::xml;

TEST_CASE("xml.parseAttributesAndChildren") {
    Document doc = Document::parse(R"(<?xml version="1.0"?>
    <tileset columns="4" tilewidth="16" tileheight="16">
      <image source="sheet.png"/>
      <tile id="0">
        <properties>
          <property name="walkable" type="bool" value="false"/>
        </properties>
        <objectgroup>
          <object x="1" y="2" width="3" height="4"/>
        </objectgroup>
      </tile>
      <wangsets>
        <wangset name="terrain">
          <wangtile tileid="0" wangid="1,0,0,0,0,0,0,0"/>
        </wangset>
      </wangsets>
    </tileset>
    )");
    REQUIRE(doc.valid());
    Element root = doc.root();
    CHECK_EQ(root.tagName(), "tileset");
    CHECK_EQ(root.getIntAttribute("columns", 0), 4);
    CHECK_EQ(root.getIntAttribute("tilewidth", 0), 16);

    auto images = root.elementsByTag("image");
    REQUIRE_EQ(images.size(), 1u);
    CHECK_EQ(images[0].getAttribute("source"), "sheet.png");

    auto tiles = root.children("tile");
    REQUIRE_EQ(tiles.size(), 1u);
    CHECK_EQ(tiles[0].getIntAttribute("id", -1), 0);
    CHECK(tiles[0].parent() == root);

    auto props = tiles[0].elementsByTag("property");
    REQUIRE_EQ(props.size(), 1u);
    CHECK_EQ(props[0].getAttribute("name"), "walkable");
    CHECK_EQ(props[0].getAttribute("value"), "false");

    auto objects = tiles[0].elementsByTag("object");
    REQUIRE_EQ(objects.size(), 1u);
    CHECK_EQ(objects[0].getIntAttribute("width", 0), 3);

    auto wang = root.elementsByTag("wangtile");
    REQUIRE_EQ(wang.size(), 1u);
    CHECK_EQ(wang[0].getAttribute("wangid"), "1,0,0,0,0,0,0,0");
}

TEST_CASE("xml.selfClosingAndEntities") {
    Document doc = Document::parse(R"(<root note="a&amp;b&lt;c">
      <child/>
    </root>)");
    REQUIRE(doc.valid());
    CHECK_EQ(doc.root().getAttribute("note"), "a&b<c");
    CHECK_EQ(doc.root().children("child").size(), 1u);
}

TEST_CASE("xml.invalidReturnsEmpty") {
    std::string error;
    Document    doc = Document::parse("<root><unclosed>", &error);
    CHECK(!doc.valid());
    CHECK(!error.empty());
    CHECK(!doc.root());
}
