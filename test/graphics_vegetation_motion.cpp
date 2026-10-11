#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationPacking.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;
namespace {
VegetationMotion isolated() {
    VegetationMotion motion;
    motion.noise   = {1, 1, {glm::vec4(0.5f, 0.5f, 1.f, 1.f)}};
    motion.bending = motion.branch = motion.rolling = motion.flutter = 0.f;
    motion.facing                                                    = 0.f;
    return motion;
}
VegetationField fullWind() {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {1, 0, 1, 0};
    field.replace(globals, {}).expect("test full wind");
    return field;
}
glm::vec3 evaluate(const VegetationField& field, const VegetationVertex& vertex, const VegetationMotion& motion) {
    auto result = deformVegetation(field, std::span(&vertex, 1), motion);
    REQUIRE(result.ok());
    return {result.value().positions[0], result.value().positions[1], result.value().positions[2]};
}
void near(glm::vec3 actual, glm::vec3 expected, float tolerance = 1e-5f) {
    CHECK(glm::length(actual - expected) < tolerance);
}
}  // namespace

TEST_CASE("graphics.vegetation.motion_bending_rotates_about_pivot") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationVertex v;
    v.pivot    = {2, 0, 3};
    v.position = {2, 2, 3};
    v.bending  = 1.f;
    // Neutral RG noise, full wind: source angle = 0.6 * 2 * amplitude.
    auto actual = evaluate(field, v, motion);
    near(actual, v.pivot + glm::vec3(2.f * std::sin(0.3f), 2.f * std::cos(0.3f), 0));
    CHECK(std::abs(glm::length(actual - v.pivot) - 2.f) < 1e-5f);
}

TEST_CASE("graphics.vegetation.motion_global_wind_is_not_local_interaction_direction") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationElement element;
    element.channel = VegetationChannel::Motion;
    element.value   = {0, 1, 1, 0};
    REQUIRE(field.replace(field.globalValues(), std::span(&element, 1)).ok());
    VegetationVertex v;
    v.position = {0, 1, 0};
    v.bending  = 1;
    near(evaluate(field, v, motion), {std::sin(0.3f), std::cos(0.3f), 0});
}

TEST_CASE("graphics.vegetation.motion_interaction_uses_fourth_power") {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {0, 1, 0, 0.5f};
    REQUIRE(field.replace(globals, {}).ok());
    auto             motion = isolated();
    VegetationVertex v;
    v.position        = {0, 1, 0};
    v.bending         = 0.5f;
    const float angle = (1.f / 1.0001f) / 16.f;
    near(evaluate(field, v, motion), {0, std::cos(angle), std::sin(angle)});
}

TEST_CASE("graphics.vegetation.motion_branch_squash_then_rolling") {
    auto field         = fullWind();
    auto motion        = isolated();
    motion.branch      = 0.4f;
    motion.rolling     = 0.3f;
    motion.branchScale = 0;
    motion.time        = 3.141592653589793 / 12.;
    VegetationVertex v;
    v.position     = {1, 2, 0};
    v.branch       = 1;
    v.boundsRadius = 2;
    near(evaluate(field, v, motion), {1.8f * std::cos(0.3f), 2.24f, -1.8f * std::sin(0.3f)});
}

TEST_CASE("graphics.vegetation.motion_flutter_texture_and_distance_fade") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.flutter = 0.2f;
    motion.noise   = {1, 1, {glm::vec4(1, 0, 0.25f, 1)}};
    VegetationVertex v;
    v.position            = {0, 1, 0};
    v.flutter             = 0.5f;
    const float amplitude = 0.1f * std::pow(0.25f, 0.6f);
    near(evaluate(field, v, motion), v.position + glm::vec3(1, -1, -0.5f) * amplitude);
    motion.camera = {0, 1000, 0};
    near(evaluate(field, v, motion), v.position);
}

TEST_CASE("graphics.vegetation.motion_batched_height_offsets_ignore_object_size") {
    auto field       = fullWind();
    auto globals     = field.globalValues();
    globals.vertex.w = 0;
    REQUIRE(field.replace(globals, {}).ok());
    auto motion      = isolated();
    motion.bending   = 0.25f;
    motion.batchMode = VegetationBatchMode::Batched;
    VegetationVertex v;
    v.position     = {0, 1, 0};
    v.bending      = 0.5f;
    v.boundsHeight = 3;
    near(evaluate(field, v, motion), {0.225f, 1, 0});
    motion.batchMode = VegetationBatchMode::Object;
    near(evaluate(field, v, motion), {0, 0, 0});
}

TEST_CASE("graphics.vegetation.motion_time_override_and_object_transform") {
    auto             field = fullWind();
    VegetationMotion motion;
    VegetationVertex v;
    v.position          = {0.1f, 1, 0.2f};
    v.bending           = 0.8f;
    v.branch            = 0.5f;
    v.flutter           = 0.5f;
    motion.time         = 4.5;
    const auto expected = evaluate(field, v, motion);
    motion.time         = 2;
    motion.timeScale    = 2;
    motion.timeOffset   = 0.5;
    motion.timeOverride = 1;
    near(evaluate(field, v, motion), expected);
    motion               = isolated();
    motion.objectToWorld = glm::translate(glm::mat4(1.f), glm::vec3(100000, 200000, 300000));
    motion.objectToWorld = glm::scale(motion.objectToWorld, glm::vec3(2, 3, 4));
    auto output          = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(output.ok());
    near({output.value().positions[0], output.value().positions[1], output.value().positions[2]},
         {100000.2f, 200003.f, 300000.8f}, 0.05f);
    near({output.value().normals[0], output.value().normals[1], output.value().normals[2]}, {0, 1, 0});
    motion.objectToWorld[1] = glm::vec4(0.f);
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.packing_retains_masks_bounds_uvs_and_pivots") {
    TvePackedVertex packed;
    packed.color     = {0.3f, 0.4f, 0.5f, 0.6f};
    packed.position  = {1, 2, 3};
    packed.texcoord0 = {0.2f, 0.7f, 1048063.f, 4194303.f};
    packed.texcoord1 = {0.1f, 0.2f, 0.3f, 0.4f};
    packed.texcoord3 = {7, 9, 8, 0};
    auto decoded     = decodeTveVegetationVertices(std::span(&packed, 1));
    REQUIRE(decoded.ok());
    const auto& v = decoded.value()[0];
    near(v.pivot, {7, 8, 9});
    CHECK(v.bending == 0.6f);
    CHECK(v.variation == 0.3f);
    CHECK(v.occlusion == 0.4f);
    CHECK(v.detail == 0.5f);
    CHECK(v.branch == 511.f / 2047.f);
    CHECK(v.flutter == 1535.f / 2047.f);
    CHECK(v.boundsHeight == 100.f);
    CHECK(v.boundsRadius == 100.f);
    CHECK(v.texcoord.x == 0.2f);
    CHECK(v.secondaryTexcoord.y == 0.2f);
    CHECK(v.detailCoord.x == 0.3f);
    packed.texcoord0.z = 1.5f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
    packed.texcoord0.z = 4194304.f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
}

TEST_CASE("graphics.vegetation.motion_invalid_noise_and_arithmetic_overflow") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    motion.noise.width = 2;
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
    motion               = isolated();
    v.position           = glm::vec3(std::numeric_limits<float>::max());
    motion.objectToWorld = glm::scale(glm::mat4(1.f), glm::vec3(100.f));
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.motion_authored_frame_follows_deformation_and_handedness") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    v.position = {0, 2, 0};
    v.normal   = {0, 0, 1};
    v.bending  = 1;
    v.tangent  = glm::vec4(1, 0, 0, -1);
    auto rest  = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(rest.ok());
    REQUIRE_EQ(rest.value().tangents.size(), size_t(3));
    near({rest.value().tangents[0], rest.value().tangents[1], rest.value().tangents[2]}, {1, 0, 0}, .001f);
    near({rest.value().bitangents[0], rest.value().bitangents[1], rest.value().bitangents[2]}, {0, -1, 0}, .001f);
    motion.bending = .4f;
    auto bent      = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(bent.ok());
    glm::vec3 t(bent.value().tangents[0], bent.value().tangents[1], bent.value().tangents[2]);
    glm::vec3 b(bent.value().bitangents[0], bent.value().bitangents[1], bent.value().bitangents[2]);
    glm::vec3 n(bent.value().normals[0], bent.value().normals[1], bent.value().normals[2]);
    CHECK(std::abs(glm::dot(n, t)) < .002f);
    CHECK(std::abs(glm::dot(n, b)) < .002f);
    CHECK(glm::dot(glm::cross(t, b), n) < -.99f);
    CHECK(glm::length(t - glm::vec3(1, 0, 0)) + glm::length(b - glm::vec3(0, -1, 0)) > .05f);
    auto repeat = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(repeat.ok());
    REQUIRE_EQ(repeat.value().tangents, bent.value().tangents);
    v.tangent->w = 0;
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
    v.tangent.reset();
    auto absent = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(absent.ok());
    REQUIRE(absent.value().tangents.empty());
}
