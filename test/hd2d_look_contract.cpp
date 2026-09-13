#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

#include "graphics/RenderSystem3D.h"
#include "hd2d/Hd2d.h"

TEST_CASE("hd2d.look.miniatureWritesCameraDofAndBloom") {
    eve::hd2d::Hd2dLook look = eve::hd2d::Hd2dLook::miniature();
    CHECK(look.getMaxBlur() > 0.f);
    CHECK(look.getBloomIntensity() > 0.f);

    eve::graphics::Camera3D *cam = eve::graphics::Camera3D::createCamera();
    REQUIRE(cam != nullptr);
    look.apply(cam);
    CHECK_EQ(cam->getDofFocusDistance(), look.getFocusDistance());
    CHECK_EQ(cam->getDofMaxBlur(), look.getMaxBlur());
    CHECK_EQ(cam->getDofFocusRange(), look.getFocusRange());
    CHECK_EQ(cam->getBloomIntensity(), look.getBloomIntensity());
    CHECK_EQ(cam->getBloomThreshold(), look.getBloomThreshold());
}

TEST_CASE("hd2d.look.rejectsInvalidFocusRange") {
    eve::hd2d::Hd2dLook look;
    bool threw = false;
    try {
        look.setFocusRange(0.f);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("hd2d.sprite.dopFixMaterialDefaults") {
    eve::hd2d::Sprite3D sprite;
    CHECK(sprite.getDepthWrite());
    CHECK(sprite.getDoubleSided());
    CHECK_EQ(sprite.getAlphaCutoff(), 0.5f);
    CHECK_EQ(sprite.getBillboardMode(), std::string("screen"));
    sprite.setBillboardMode("yaw");
    CHECK_EQ(sprite.getBillboardMode(), std::string("yaw"));
    sprite.setAlphaCutoff(0.7f);
    CHECK_EQ(sprite.getAlphaCutoff(), 0.7f);
    sprite.setDepthWrite(false);
    CHECK(!sprite.getDepthWrite());
}
