#include "graphics/ShadowScheme.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <vector>

using namespace eve::graphics;

namespace {

ShadowPagingView makeViewAtOrigin() {
    ShadowPagingView view{};
    view.eye = glm::vec3(0.f, 2.f, 8.f);
    view.viewProj = glm::mat4(1.f);  // identity: world ≈ clip for coarse tests
    view.valid = true;
    return view;
}

Light3D* makeSpot(float x, float y, float z, float intensity) {
    Light3D* spot = Light3D::createLight("spot");
    spot->setPosition(x, y, z);
    spot->setDirection(0.f, -1.f, 0.f);
    spot->setColor(1.f, 1.f, 1.f, intensity);
    spot->setRadius(10.f);
    spot->setSpotAngle(35.f);
    spot->setCastShadow(true);
    spot->setShadowMethod("perspective");
    return spot;
}

}  // namespace

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
    resetShadowLocalPageCache();
    Light3D* sun = Light3D::createLight("dir");
    sun->setDirection(-0.4f, 0.9f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 2.f);
    sun->setCastShadow(true);
    sun->setShadowMethod("csm");

    Light3D* spot = makeSpot(0.f, 4.f, 0.f, 3.f);

    Light3D* point = Light3D::createLight("point");
    point->setPosition(2.f, 2.f, 2.f);
    point->setColor(1.f, 1.f, 1.f, 1.f);
    point->setRadius(6.f);
    point->setCastShadow(true);
    point->setShadowMethod("cube");

    Light3D::Data* sunData   = &*sun->data();
    Light3D::Data* spotData  = &*spot->data();
    Light3D::Data* pointData = &*point->data();
    std::vector<Light3D::Data*> lights{sunData, spotData, pointData};
    std::vector<bool>           isPoint{false, true, true};
    ShadowSchemeSettings        settings = ShadowSchemeSettings::current();
    settings.enableDirectionalCsm        = true;
    settings.enableSpotPerspective       = true;
    settings.enablePointCube             = false;
    settings.maxSpotShadowCasters        = 2;
    settings.enableLocalPaging           = false;

    Light3D::Data*               directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, ShadowPagingView{}, directional, locals);
    CHECK(directional == sunData);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == spotData);
    CHECK_EQ(locals[0].layer, ShadowConfig::kCascades);
    CHECK(locals[0].needsUpdate);
    CHECK_EQ(spotData->shadowLocalSlot, 0);
    CHECK_EQ(pointData->shadowLocalSlot, -1);

    const glm::mat4 vp = buildSpotShadowVP(glm::vec3(0.f, 4.f, 0.f), glm::vec3(0.f, -1.f, 0.f), 12.f, 35.f);
    const glm::vec4 origin = vp * glm::vec4(0.f, 0.f, 0.f, 1.f);
    CHECK(origin.w > 0.f);

    auto face = buildPointShadowFaceVP(glm::vec3(0.f), 0, 8.f);
    CHECK(face.ok());
    CHECK(!buildPointShadowFaceVP(glm::vec3(0.f), 6, 8.f).ok());
}

TEST_CASE("graphics.shadow_scheme.pages_more_spots_than_atlas_slots") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings = ShadowSchemeSettings::current();
    settings.enableSpotPerspective = true;
    settings.enableDirectionalCsm = false;
    settings.maxSpotShadowCasters = 2;
    settings.enableLocalPaging = true;
    settings.hysteresisBonus = 0.f;
    settings.maxLocalUpdatesPerFrame = 4;

    // Five spots near the camera; only two atlas slots.
    Light3D* a = makeSpot(0.f, 2.f, 0.f, 10.f);
    Light3D* b = makeSpot(1.f, 2.f, 0.f, 8.f);
    Light3D* c = makeSpot(2.f, 2.f, 0.f, 6.f);
    Light3D* d = makeSpot(3.f, 2.f, 0.f, 4.f);
    Light3D* e = makeSpot(4.f, 2.f, 0.f, 2.f);
    std::vector<Light3D::Data*> lights{&*a->data(), &*b->data(), &*c->data(), &*d->data(), &*e->data()};
    std::vector<bool> isPoint(5, true);

    Light3D::Data* directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, makeViewAtOrigin(), directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    CHECK(locals[0].light == &*a->data());
    CHECK(locals[1].light == &*b->data());
    CHECK_EQ(a->data()->shadowLocalSlot, 0);
    CHECK_EQ(b->data()->shadowLocalSlot, 1);
    CHECK_EQ(c->data()->shadowLocalSlot, -1);
}

TEST_CASE("graphics.shadow_scheme.hysteresis_keeps_sticky_slot") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings = ShadowSchemeSettings::current();
    settings.enableSpotPerspective = true;
    settings.maxSpotShadowCasters = 1;
    settings.enableLocalPaging = false;  // intensity-only scores for a clear sticky check
    settings.hysteresisBonus = 0.5f;     // 5 * 1.5 = 7.5 > 6
    settings.maxLocalUpdatesPerFrame = 4;

    Light3D* resident = makeSpot(0.f, 2.f, 0.f, 5.f);
    std::vector<Light3D::Data*> lights{&*resident->data()};
    std::vector<bool> isPoint{true};
    const ShadowPagingView view{};

    Light3D::Data* directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == &*resident->data());

    // Challenger is brighter raw (6 > 5), but hadSlot × hysteresis keeps the resident (7.5).
    Light3D* challenger = makeSpot(1.f, 2.f, 0.f, 6.f);
    lights = {&*resident->data(), &*challenger->data()};
    isPoint = {true, true};
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == &*resident->data());
}

TEST_CASE("graphics.shadow_scheme.update_budget_skips_stable_slots") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings = ShadowSchemeSettings::current();
    settings.enableSpotPerspective = true;
    settings.maxSpotShadowCasters = 2;
    settings.enableLocalPaging = true;
    settings.maxLocalUpdatesPerFrame = 1;

    Light3D* a = makeSpot(0.f, 2.f, 0.f, 10.f);
    Light3D* b = makeSpot(1.f, 2.f, 0.f, 9.f);
    std::vector<Light3D::Data*> lights{&*a->data(), &*b->data()};
    std::vector<bool> isPoint{true, true};
    const ShadowPagingView view = makeViewAtOrigin();

    Light3D::Data* directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    // First frame: both new, but budget=1 → only one needsUpdate.
    int updating = 0;
    for (const auto& slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 1);

    // Second frame, unchanged lights: no dirty updates.
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    updating = 0;
    for (const auto& slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 0);

    // Move one light → dirty; budget still 1.
    a->setPosition(0.2f, 2.f, 0.f);
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    updating = 0;
    for (const auto& slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 1);
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
