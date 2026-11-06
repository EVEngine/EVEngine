#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/DirectedRounding.h"
#include "graphics/multiscale/FarFlux.h"
#include "graphics/multiscale/SphereView.h"
#include "spatial/multiscale/CellPosition.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics::multiscale;
using eve::spatial::CellPosition;

TEST_CASE("graphics.multiscale.directedRoundingMatchesStandardBitsAndBoundaryExceptions") {
    auto check = [](std::uint64_t bits) {
        const double input = std::bit_cast<double>(bits);
        return std::bit_cast<std::uint64_t>(detail::roundUp(input)) ==
                   std::bit_cast<std::uint64_t>(std::nextafter(input, std::numeric_limits<double>::infinity())) &&
               std::bit_cast<std::uint64_t>(detail::roundDown(input)) ==
                   std::bit_cast<std::uint64_t>(std::nextafter(input, -std::numeric_limits<double>::infinity()));
    };
    for (const auto bits : {UINT64_C(0), UINT64_C(1), UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000),
                            UINT64_C(0x0010000000000001), UINT64_C(0x3ff0000000000000), UINT64_C(0x7feffffffffffffe),
                            UINT64_C(0x7fefffffffffffff), UINT64_C(0x7ff0000000000000), UINT64_C(0x7ff8000000000021)}) {
        REQUIRE(check(bits));
        REQUIRE(check(bits | UINT64_C(0x8000000000000000)));
    }
    std::uint64_t state     = UINT64_C(0x7363616c655f726e);
    bool          identical = true;
    for (unsigned i = 0; i < 1000000; ++i) {
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        identical &= check(state);
    }
    REQUIRE(identical);
    for (double input : {0.0, -0.0, std::numeric_limits<double>::min(), -std::numeric_limits<double>::min(),
                         std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()}) {
        std::feclearexcept(FE_ALL_EXCEPT);
        volatile double expected      = std::nextafter(input, std::numeric_limits<double>::infinity());
        const int       expectedFlags = std::fetestexcept(FE_ALL_EXCEPT);
        std::feclearexcept(FE_ALL_EXCEPT);
        volatile double actual      = detail::roundUp(input);
        const int       actualFlags = std::fetestexcept(FE_ALL_EXCEPT);
        REQUIRE(std::bit_cast<std::uint64_t>(static_cast<double>(actual)) ==
                std::bit_cast<std::uint64_t>(static_cast<double>(expected)));
        REQUIRE(actualFlags == expectedFlags);
    }
    std::feclearexcept(FE_ALL_EXCEPT);
}

TEST_CASE("graphics.multiscale.farFluxPreservesBrightDistantSourcesAndAggregates") {
    AngularView view;
    view.cellExponent = 900;
    FarFluxPolicy policy;
    policy.width = policy.height = 4;
    std::vector<FarEmitter> emitters(3);
    emitters[0].position.cell[2] = 1000000000000000000LL;
    emitters[0].powerExponent = 1920;
    emitters[0].rgbWeights = {1, 0, 0};
    emitters[1] = emitters[0];
    emitters[1].rgbWeights = {0, 0.5, 0};
    emitters[2] = emitters[0];
    emitters[2].position.cell[0] = 4000000000000000000LL;
    auto frame = FarFluxFrame::build(emitters, {}, view, policy);
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().pixels().size(), 16u);
    const double expected = std::ldexp(1.0, 120) / (4 * std::numbers::pi_v<double> * 1e36);
    double red = 0, green = 0, blue = 0;
    for (const auto& pixel : frame.value().pixels()) {
        red += pixel[0];
        green += pixel[1];
        blue += pixel[2];
    }
    REQUIRE(std::abs(red / expected - 1) < 1e-12);
    REQUIRE(std::abs(green / expected - 0.5) < 1e-12);
    REQUIRE_EQ(blue, 0.0);
}

TEST_CASE("graphics.multiscale.farFluxRejectsSingularAndUnrepresentableInputs") {
    AngularView view;
    FarFluxPolicy policy;
    policy.width = policy.height = 4;
    FarEmitter emitter;
    REQUIRE(!FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy).ok());
    emitter.position.cell[2] = 1;
    emitter.powerExponent = -3000;
    REQUIRE(!FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy).ok());
    emitter.powerExponent = 0;
    emitter.rgbWeights[0] = -1;
    REQUIRE(!FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy).ok());
    emitter.rgbWeights[0] = 1;
    policy.maxEmitters = 0;
    REQUIRE(!FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy).ok());
}

TEST_CASE("graphics.multiscale.referenceSphereFramesRejectUnsupportedPrecisionBeforeRendering") {
    std::array<CellPosition, 1> centers{};
    centers[0].cell[2] = 10;
    std::array<SphereAppearance, 1> appearance{};
    AngularView                     view;
    auto                            accepted = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(accepted.ok());
    appearance[0].radiusMantissa = 1e-8;
    auto tiny                    = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(!tiny.ok());
    appearance[0].radiusMantissa = 10;
    auto surface                 = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(!surface.ok());
    appearance[0].radiusMantissa = 1;
    appearance[0].color[0]       = std::numeric_limits<float>::infinity();
    auto invalidColor            = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(!invalidColor.ok());
    auto invalidCount = SphereFrame::build(centers, {}, {}, view, 1);
    REQUIRE(!invalidCount.ok());
    auto empty = SphereFrame::build({}, {}, {}, view, 1);
    REQUIRE(empty.ok());
}

TEST_CASE("graphics.multiscale.sameAngleSurvivesExtremeScaleAndLargeSharedAddresses") {
    CellPosition camera, center;
    center.cell[2] = 10;
    AngularView view;
    auto        reference = projectSphere(center, camera, 1, 0, view);
    REQUIRE(reference.ok());
    REQUIRE(reference.value().visibility == AngularVisibility::Projected);
    REQUIRE(reference.value().minimum[0] < -0.1);
    REQUIRE(reference.value().maximum[0] > 0.1);
    for (int exponent : {-900, 900}) {
        view.cellExponent = exponent;
        auto scaled       = projectSphere(center, camera, 1, exponent, view);
        REQUIRE(scaled.ok());
        REQUIRE(scaled.value().minimum == reference.value().minimum);
        REQUIRE(scaled.value().maximum == reference.value().maximum);
    }
    view.cellExponent = 0;
    camera.cell       = {INT64_MAX - 100, INT64_MAX - 100, INT64_MAX - 100};
    center.cell       = {INT64_MAX - 100, INT64_MAX - 100, INT64_MAX - 90};
    auto nearby       = projectSphere(center, camera, 1, 0, view);
    REQUIRE(nearby.ok());
    REQUIRE(nearby.value().minimum == reference.value().minimum);
    REQUIRE(nearby.value().maximum == reference.value().maximum);
    camera            = {};
    center            = {};
    center.cell[2]    = 1000000000000000000LL;
    view.cellExponent = 900;
    auto far          = projectSphere(center, camera, 1e17, 900, view);
    REQUIRE(far.ok());
    REQUIRE(far.value().visibility == AngularVisibility::Projected);
    REQUIRE(std::abs(far.value().maximum[0] - reference.value().maximum[0]) < 1e-12);
}

TEST_CASE("graphics.multiscale.projectedRectangleEnclosesSampledSphereAndRotatedView") {
    CellPosition camera, center;
    center.cell = {3, -2, 12};
    AngularView  view;
    const double c          = std::sqrt(0.5);
    view.right              = {c, 0, -c};
    view.forward            = {c, 0, c};
    view.aspect             = 16.0 / 9.0;
    view.tanHalfVerticalFov = 0.8;
    auto projected          = projectSphere(center, camera, 2, 0, view);
    REQUIRE(projected.ok());
    REQUIRE(projected.value().visibility == AngularVisibility::Projected);
    for (int latitude = -10; latitude <= 10; ++latitude)
        for (int longitude = 0; longitude < 40; ++longitude) {
            const double phi   = latitude * 0.15707963267948966;
            const double theta = longitude * 0.15707963267948966;
            const double x     = 3 + 2 * std::cos(phi) * std::cos(theta);
            const double y     = -2 + 2 * std::sin(phi);
            const double z     = 12 + 2 * std::cos(phi) * std::sin(theta);
            const double depth = c * x + c * z;
            const double u     = (c * x - c * z) / (depth * view.aspect * view.tanHalfVerticalFov);
            const double v     = y / (depth * view.tanHalfVerticalFov);
            REQUIRE(u >= projected.value().minimum[0]);
            REQUIRE(u <= projected.value().maximum[0]);
            REQUIRE(v >= projected.value().minimum[1]);
            REQUIRE(v <= projected.value().maximum[1]);
        }
}

TEST_CASE("graphics.multiscale.uncertainNearPlaneIsRetainedAndInvalidViewsReject") {
    CellPosition camera, center;
    AngularView  view;
    auto         containing = projectSphere(center, camera, 1, 0, view);
    REQUIRE(containing.ok());
    REQUIRE(containing.value().visibility == AngularVisibility::IntersectsCameraPlane);
    center.cell[2] = -10;
    auto behind    = projectSphere(center, camera, 1, 0, view);
    REQUIRE(behind.ok());
    REQUIRE(behind.value().visibility == AngularVisibility::Outside);
    center.cell = {100, 0, 10};
    auto side   = projectSphere(center, camera, 1, 0, view);
    REQUIRE(side.ok());
    REQUIRE(side.value().visibility == AngularVisibility::Outside);
    center.cell = {0, 0, 10};
    auto huge   = projectSphere(center, camera, std::numeric_limits<double>::max(), 900, view);
    REQUIRE(huge.ok());
    REQUIRE(huge.value().visibility == AngularVisibility::IntersectsCameraPlane);
    view.right   = {0, 1, 0};
    auto invalid = projectSphere(center, camera, 1, 0, view);
    REQUIRE(!invalid.ok());
    view               = {};
    center.fraction[0] = 0.5;
    auto noncanonical  = projectSphere(center, camera, 1, 0, view);
    REQUIRE(!noncanonical.ok());
}
