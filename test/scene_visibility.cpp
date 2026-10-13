#include <limits>
#include "common/config.h"
#include "graphics/SceneVisibility.h"
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#else
#include "graphics/vulkan/Graphics.h"
#endif
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
eve::Value request() {
    using V = eve::Value;
    return V::Object{
        {"schemaId", "eve.scene.visibility-request"},
        {"schemaVersion", 1},
        {"sourceRevision", 3},
        {"candidateGeneration", 7},
        {"width", 64},
        {"height", 64},
        {"camera", V::Object{{"eye", V::Array{0, 0, 10}},
                             {"target", V::Array{0, 0, 0}},
                             {"up", V::Array{0, 1, 0}},
                             {"fov", 60},
                             {"near", 0.1},
                             {"far", 100}}},
        {"draws", V::Array{V::Object{{"object", "target"},
                                     {"entityId", 1},
                                     {"generation", 1},
                                     {"model", V::Array{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}}}},
        {"targets", V::Array{"target"}}};
}
}  // namespace
TEST_CASE("graphics.visibility.request_rejects_unknown_nonfinite_and_pixel_budget") {
    auto valid = request();
    REQUIRE(eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    auto& root     = *valid.getIf<eve::Value::Object>();
    root["width"]  = 2048;
    root["height"] = 2048;
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    valid                                           = request();
    (*valid.getIf<eve::Value::Object>())["unknown"] = 1;
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    valid         = request();
    auto& camera  = *valid.getIf<eve::Value::Object>()->at("camera").getIf<eve::Value::Object>();
    camera["fov"] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
}
TEST_CASE("graphics.visibility.request_rejects_degenerate_camera_and_duplicate_handles") {
    auto  valid      = request();
    auto& root       = *valid.getIf<eve::Value::Object>();
    auto& camera     = *root.at("camera").getIf<eve::Value::Object>();
    camera["target"] = camera.at("eye");
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    valid       = request();
    auto& draws = *valid.getIf<eve::Value::Object>()->at("draws").getIf<eve::Value::Array>();
    draws.push_back(draws.front());
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    valid                                           = request();
    (*valid.getIf<eve::Value::Object>())["targets"] = eve::Value::Array{"missing"};
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
}

TEST_CASE("graphics.visibility.backend_absence_and_stale_query_fail_without_mutation") {
    using Base = eve::graphics::Graphics;
#ifdef EVENGINE_WEBGPU
    eve::graphics::webgpu::Graphics graphics;
#else
    eve::graphics::vulkan::Graphics graphics;
#endif
    auto absent = graphics.Base::captureVisibilityPixels({}, glm::mat4(1.f), 16, 16);
    REQUIRE(!absent.ok());
    auto prepared = eve::graphics::SceneVisibilityQuery::prepare(request());
    REQUIRE(prepared.ok());
    auto stale = prepared.value().measure(graphics);
    REQUIRE(!stale.ok());
    auto repeated = prepared.value().measure(graphics);
    REQUIRE(!repeated.ok());
}

TEST_CASE("graphics.visibility.regions_require_v2_and_strict_distinct_local_definitions") {
    using V         = eve::Value;
    const V region  = V::Object{{"schemaId", "eve.scene.local-region"},
                                {"schemaVersion", 1},
                                {"kind", "local_box"},
                                {"id", "window"},
                                {"geometryRevision", "recipe"},
                                {"minimum", V::Array{-0.5, 0.0, -0.6}},
                                {"maximum", V::Array{0.5, 0.5, 0.6}}};
    auto    input   = request();
    auto&   root    = *input.getIf<V::Object>();
    V       target  = V::Object{{"object", "target"}, {"region", region}};
    root["targets"] = V::Array{"target", target};
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(input).ok());
    root["schemaVersion"] = 2;
    REQUIRE(eve::graphics::SceneVisibilityQuery::prepare(input).ok());
    root["targets"] = V::Array{target, target};
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(input).ok());
    (*target.getIf<V::Object>()->at("region").getIf<V::Object>())["maximum"] = V::Array{0.0, 0.0, 0.0};
    root["targets"]                                                          = V::Array{target};
    REQUIRE(!eve::graphics::SceneVisibilityQuery::prepare(input).ok());
}
