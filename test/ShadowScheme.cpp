#include "graphics/ShadowScheme.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <vector>

using namespace eve::graphics;

TEST_CASE("graphics.shadow_scheme.parse_and_resolve_methods") {
    CHECK(parseShadowMethod("auto").ok());
    CHECK_EQ(static_cast<int>(parseShadowMethod("csm").value()),
             static_cast<int>(ShadowMethod::CascadedDirectional));
    CHECK_EQ(static_cast<int>(parseShadowMethod("perspective").value()),
             static_cast<int>(ShadowMethod::PerspectiveSpot));
    CHECK_EQ(static_cast<int>(parseShadowMethod("cube").value()),
             static_cast<int>(ShadowMethod::CubePoint));
    CHECK(!parseShadowMethod("nope").ok());

    CHECK_EQ(static_cast<int>(resolveShadowMethod("dir", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::CascadedDirectional));
    CHECK_EQ(static_cast<int>(resolveShadowMethod("spot", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::PerspectiveSpot));
    CHECK_EQ(static_cast<int>(resolveShadowMethod("point", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::CubePoint));
    CHECK(!resolveShadowMethod("dir", ShadowMethod::PerspectiveSpot).ok());
    CHECK(!resolveShadowMethod("spot", ShadowMethod::CascadedDirectional).ok());
    CHECK_EQ(shadowMethodName(ShadowMethod::PerspectiveSpot), "perspective");
}

TEST_CASE("graphics.shadow_scheme.selects_spot_slots_and_builds_vp") {
    Light3D* sun = Light3D::createLight("dir");
    sun->setDirection(-0.4f, 0.9f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 2.f);
    sun->setCastShadow(true);
    sun->setShadowMethod("csm");

    Light3D* spot = Light3D::createLight("spot");
    spot->setPosition(0.f, 4.f, 0.f);
    spot->setDirection(0.f, -1.f, 0.f);
    spot->setColor(1.f, 0.9f, 0.8f, 3.f);
    spot->setRadius(12.f);
    spot->setSpotAngle(35.f);
    spot->setCastShadow(true);
    spot->setShadowMethod("perspective");

    Light3D* point = Light3D::createLight("point");
    point->setPosition(2.f, 2.f, 2.f);
    point->setColor(1.f, 1.f, 1.f, 1.f);
    point->setRadius(6.f);
    point->setCastShadow(true);
    point->setShadowMethod("cube");

    std::vector<Light3D::Data*> lights{sun->data(), spot->data(), point->data()};
    std::vector<bool>           isPoint{false, true, true};
    ShadowSchemeSettings        settings = ShadowSchemeSettings::current();
    settings.enableDirectionalCsm        = true;
    settings.enableSpotPerspective       = true;
    settings.enablePointCube             = false;
    settings.maxSpotShadowCasters        = 2;

    Light3D::Data*               directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, directional, locals);
    CHECK(directional == sun->data());
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == spot->data());
    CHECK_EQ(locals[0].layer, ShadowConfig::kCascades);
    CHECK_EQ(spot->data()->shadowLocalSlot, 0);
    CHECK_EQ(point->data()->shadowLocalSlot, -1);

    const glm::mat4 vp = buildSpotShadowVP(glm::vec3(0.f, 4.f, 0.f), glm::vec3(0.f, -1.f, 0.f), 12.f, 35.f);
    const glm::vec4 origin = vp * glm::vec4(0.f, 0.f, 0.f, 1.f);
    CHECK(origin.w > 0.f);

    auto face = buildPointShadowFaceVP(glm::vec3(0.f), 0, 8.f);
    CHECK(face.ok());
    CHECK(!buildPointShadowFaceVP(glm::vec3(0.f), 6, 8.f).ok());
}

TEST_CASE("graphics.light3d_spot_and_shadow_method_api") {
    Light3D* spot = Light3D::createLight("spot");
    CHECK_EQ(spot->getType(), "spot");
    spot->setSpotAngle(40.f);
    spot->setSpotSoftness(0.5f);
    CHECK_EQ(spot->getSpotAngle(), 40.f);
    CHECK_EQ(spot->getSpotSoftness(), 0.5f);
    spot->setShadowMethod("perspective");
    CHECK_EQ(spot->getShadowMethod(), "perspective");
    spot->setShadowMethod("auto");
    CHECK_EQ(spot->getShadowMethod(), "auto");
    spot->setShadowMethod("not-a-method");
    CHECK_EQ(spot->getShadowMethod(), "auto");
}
