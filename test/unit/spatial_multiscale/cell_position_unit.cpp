#include "spatial/multiscale/CellPosition.h"
#include "spatial/multiscale/FrameGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace eve::spatial;

TEST_CASE("spatial.frameGraph.cancelsHugeCommonAncestorBeforeProjection") {
    std::vector<FrameDefinition> definitions(4);
    definitions[1].translation.cell[0] = INT64_MAX;
    definitions[2]                     = {1, {{1, 0, 0}, {}}, -20};
    definitions[3]                     = {1, {{2, 0, 0}, {}}, -10};
    auto graph                         = FrameGraph::build(definitions, 4);
    REQUIRE(graph.ok());
    auto a = graph.value().frame(2);
    auto b = graph.value().frame(3);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto relative = graph.value().project({a.value(), {{1, 0, 0}, {}}}, {b.value(), {{1, 0, 0}, {}}}, -20, 1e-7);
    REQUIRE(relative.ok());
    CHECK_EQ(relative.value().offset[0], -1049599.0);
    auto reverse = graph.value().project({b.value(), {{1, 0, 0}, {}}}, {a.value(), {{1, 0, 0}, {}}}, -20, 1e-7);
    REQUIRE(reverse.ok());
    CHECK_EQ(reverse.value().offset[0], -relative.value().offset[0]);
    auto root = graph.value().frame(0);
    REQUIRE(root.ok());
    auto tooPrecise = graph.value().project({a.value(), {}}, {root.value(), {}}, 0, 1e-7);
    CHECK(!tooPrecise.ok());
}

TEST_CASE("spatial.frameGraph.identityRejectsForeignAndRebuiltGraphs") {
    const std::array<FrameDefinition, 1> definition{};
    auto                                 first  = FrameGraph::build(definition, 1);
    auto                                 second = FrameGraph::build(definition, 1);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    auto a = first.value().frame(0);
    auto b = second.value().frame(0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto foreign = first.value().project({a.value(), {}}, {b.value(), {}}, 0, 1e-9);
    REQUIRE(!foreign.ok());
    CHECK(foreign.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    first.value() = std::move(second.value());
    auto stale    = first.value().project({a.value(), {}}, {a.value(), {}}, 0, 1e-9);
    CHECK(!stale.ok());
    auto retained = first.value().project({b.value(), {}}, {b.value(), {}}, 0, 1e-9);
    REQUIRE(retained.ok());
    auto moved = second.value().project({b.value(), {}}, {b.value(), {}}, 0, 1e-9);
    CHECK(!moved.ok());
}

TEST_CASE("spatial.frameGraph.rejectsInvalidTopologyScaleDepthAndBudget") {
    std::vector<FrameDefinition> definitions(2);
    definitions[1].parentIndex = 1;
    auto cycle                 = FrameGraph::build(definitions, 2);
    REQUIRE(!cycle.ok());
    definitions[1].parentIndex = 0;
    definitions[1].exponent    = 901;
    auto scale                 = FrameGraph::build(definitions, 2);
    CHECK(!scale.ok());
    definitions[1].exponent = 0;
    auto budget             = FrameGraph::build(definitions, 1);
    CHECK(!budget.ok());
    definitions[0].translation.fraction[0] = 0.25;
    auto root                              = FrameGraph::build(definitions, 2);
    CHECK(!root.ok());
    definitions.assign(66, {});
    for (std::size_t i = 1; i < definitions.size(); ++i) definitions[i].parentIndex = i - 1;
    auto depth = FrameGraph::build(definitions, 66);
    CHECK(!depth.ok());
}

TEST_CASE("spatial.cellPosition.normalizationCarriesBothDirectionsExactly") {
    for (int cell = -4; cell <= 4; ++cell)
        for (int offset = -100; offset <= 100; ++offset) {
            const double fraction = static_cast<double>(offset) / 8.0;
            auto         result   = normalizeCellPosition({{cell, 0, 0}, {fraction, 0, 0}});
            REQUIRE(result.ok());
            const auto& p = result.value();
            CHECK((p.fraction[0] >= -0.5 && p.fraction[0] < 0.5));
            CHECK_EQ(static_cast<double>(p.cell[0]) + p.fraction[0], cell + fraction);
            auto again = normalizeCellPosition(p);
            REQUIRE(again.ok());
            CHECK_EQ(again.value().cell[0], p.cell[0]);
            CHECK_EQ(again.value().fraction[0], p.fraction[0]);
        }
}

TEST_CASE("spatial.cellPosition.normalizationPreservesAdjacentBoundaryFractions") {
    const double below  = std::nextafter(0.5, 0.0);
    auto         result = normalizeCellPosition({{INT64_MAX, INT64_MIN, 0}, {below, -0.5, 0.5}});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().cell[0], INT64_MAX);
    CHECK_EQ(result.value().fraction[0], below);
    CHECK_EQ(result.value().cell[1], INT64_MIN);
    CHECK_EQ(result.value().fraction[1], -0.5);
    CHECK_EQ(result.value().cell[2], int64_t{1});
    CHECK_EQ(result.value().fraction[2], -0.5);
    auto carry = normalizeCellPosition({{0, 0, 0}, {std::nextafter(-0.5, -1.0), 0, 0}});
    REQUIRE(carry.ok());
    CHECK_EQ(carry.value().cell[0], int64_t{-1});
    CHECK_EQ(carry.value().fraction[0], std::nextafter(-0.5, -1.0) + 1.0);
}

TEST_CASE("spatial.cellPosition.normalizationChecksFullWidthCarryAndOverflow") {
    auto cancelled = normalizeCellPosition({{INT64_MIN, INT64_MAX, 0}, {0x1p63, -0x1p63, -0.0}});
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value().cell[0], int64_t{0});
    CHECK_EQ(cancelled.value().cell[1], int64_t{-1});
    CHECK(!std::signbit(cancelled.value().fraction[2]));
    auto upper = normalizeCellPosition({{INT64_MAX, 0, 0}, {0.5, 0, 0}});
    REQUIRE(!upper.ok());
    CHECK(upper.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Unsupported);
    auto lower = normalizeCellPosition({{INT64_MIN, 0, 0}, {-0.75, 0, 0}});
    CHECK(!lower.ok());
    auto enormous = normalizeCellPosition({{}, {0x1p64, 0, 0}});
    CHECK(!enormous.ok());
    auto invalid = normalizeCellPosition({{}, {0, 0, std::numeric_limits<double>::infinity()}});
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("spatial.cellPosition.preservesTinyOffsetAtHugeAddress") {
    CellPosition origin{{INT64_MAX - 1, INT64_MIN + 1, 0}, {0.0, 0.0, 0.0}};
    CellPosition target = origin;
    target.fraction     = {std::ldexp(1.0, -40), -std::ldexp(1.0, -40), 0.125};
    auto result         = projectCellRelative(target, origin, {0, 0, 1e-12});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().offset[0], target.fraction[0]);
    CHECK_EQ(result.value().offset[1], target.fraction[1]);
    CHECK_EQ(result.value().offset[2], 0.125);
    CHECK(result.value().absoluteErrorBound[0] < 1e-25);
}

TEST_CASE("spatial.cellPosition.crossesCellBoundaryWithoutAbsoluteRounding") {
    CellPosition origin{{INT64_MAX - 1, -5, 0}, {0.49, 0.0, 0.0}};
    CellPosition target{{INT64_MAX, -4, 0}, {-0.49, 0.0, 0.0}};
    auto         result = projectCellRelative(target, origin, {10, 0, 1e-10});
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().offset[0] - 20.48) < 1e-12);
    CHECK_EQ(result.value().offset[1], 1024.0);
    auto reverse = projectCellRelative(origin, target, {10, 0, 1e-10});
    REQUIRE(reverse.ok());
    CHECK_EQ(reverse.value().offset[0], -result.value().offset[0]);
}

TEST_CASE("spatial.cellPosition.fullSignedSpanHasExplicitPrecisionBudget") {
    CellPosition origin{{INT64_MIN, 0, 0}, {}};
    CellPosition target{{INT64_MAX, 0, 0}, {}};
    auto         rejected = projectCellRelative(target, origin, {0, 0, 1e-6});
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Unsupported);
    auto accepted = projectCellRelative(target, origin, {0, 0, 1e6});
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value().offset[0], std::ldexp(1.0, 64));
    CHECK(accepted.value().absoluteErrorBound[0] >= 1.0);
}

TEST_CASE("spatial.cellPosition.rejectsInvalidFractionsAndPolicies") {
    CellPosition bad;
    bad.fraction[2] = 0.5;
    auto fraction   = projectCellRelative(bad, {}, {});
    REQUIRE(!fraction.ok());
    CHECK(fraction.status().primaryDiagnostic()->code() == eve::DiagnosticCode::InvalidArgument);
    bad.fraction[2] = std::numeric_limits<double>::quiet_NaN();
    auto nan        = projectCellRelative(bad, {}, {});
    CHECK(!nan.ok());
    auto scale = projectCellRelative({}, {}, {901, 0, 1e-6});
    CHECK(!scale.ok());
    auto tolerance = projectCellRelative({}, {}, {0, 0, 0.0});
    CHECK(!tolerance.ok());
}

TEST_CASE("spatial.cellPosition.supportsExtremeUnitsAndReportsRangeLoss") {
    CellPosition target{{1, 0, 0}, {}};
    auto         large = projectCellRelative(target, {}, {900, 900, 1e-12});
    REQUIRE(large.ok());
    CHECK_EQ(large.value().offset[0], 1.0);
    auto small = projectCellRelative(target, {}, {-900, -900, 1e-12});
    REQUIRE(small.ok());
    CHECK_EQ(small.value().offset[0], 1.0);
    auto overflow = projectCellRelative(target, {}, {900, -900, 1e-12});
    CHECK(!overflow.ok());
    auto underflow = projectCellRelative(target, {}, {-900, 900, 1e-12});
    REQUIRE(underflow.ok());
    CHECK_EQ(underflow.value().offset[0], 0.0);
    CHECK(underflow.value().absoluteErrorBound[0] > 0.0);
    auto exactZero = projectCellRelative(target, target, {900, -900, 1e-12});
    REQUIRE(exactZero.ok());
    CHECK_EQ(exactZero.value().absoluteErrorBound[0], 0.0);
}

namespace {
constexpr std::array<double, 9> quarterZ{0, -1, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<double, 9> quarterX{1, 0, 0, 0, 0, -1, 0, 1, 0};
}  // namespace
TEST_CASE("spatial.frameGraph.orientedSharedAncestorPreservesTinyLocalDifference") {
    std::vector<FrameDefinition> definitions(3);
    definitions[1].translation.cell = {INT64_MAX, INT64_MIN, 0};
    definitions[1].exponent         = 900;
    definitions[1].rotation         = quarterZ;
    definitions[2].parentIndex      = 1;
    definitions[2].exponent         = -900;
    auto graph                      = FrameGraph::build(definitions, 3);
    REQUIRE(graph.ok());
    auto frame = graph.value().frame(2);
    REQUIRE(frame.ok());
    CellPosition origin{{INT64_MAX, INT64_MIN, 0}, {}};
    auto         target = origin;
    target.fraction     = {0.125, 0.25, -0.125};
    auto projected      = graph.value().project({frame.value(), target}, {frame.value(), origin}, 0, 1e-12);
    REQUIRE(projected.ok());
    const std::array<double, 3> expected{-0.25, 0.125, -0.125};
    for (unsigned axis = 0; axis < 3; ++axis)
        REQUIRE(std::abs(projected.value().offset[axis] - expected[axis]) <=
                projected.value().absoluteErrorBound[axis]);
}
TEST_CASE("spatial.frameGraph.orientedBranchesRotateTranslationsInParentAxes") {
    std::vector<FrameDefinition> definitions(4);
    definitions[1].translation.cell = {INT64_MAX, INT64_MIN, 0};
    definitions[1].exponent         = 800;
    definitions[2].parentIndex      = 1;
    definitions[2].translation.cell = {10, 0, 0};
    definitions[2].rotation         = quarterZ;
    definitions[2].exponent         = 2;
    definitions[3].parentIndex      = 1;
    definitions[3].translation.cell = {-5, 3, 1};
    definitions[3].rotation         = quarterX;
    definitions[3].exponent         = -1;
    auto graph                      = FrameGraph::build(definitions, 4);
    REQUIRE(graph.ok());
    auto a = graph.value().frame(2), b = graph.value().frame(3);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto result = graph.value().project({a.value(), {{1, 2, 3}, {}}}, {b.value(), {{2, 4, 6}, {}}}, 800, 1e-10);
    REQUIRE(result.ok());
    const std::array<double, 3> expected{6, 4, 9};
    for (unsigned axis = 0; axis < 3; ++axis)
        REQUIRE(std::abs(result.value().offset[axis] - expected[axis]) <= result.value().absoluteErrorBound[axis]);
}
TEST_CASE("spatial.frameGraph.composedArbitraryRotationBoundsContainReference") {
    std::vector<FrameDefinition> definitions(33);
    for (std::size_t i = 1; i < definitions.size(); ++i) {
        definitions[i].parentIndex = i - 1;
        const double angle         = static_cast<double>(i) * 0.07;
        const double c = std::cos(angle), s = std::sin(angle);
        definitions[i].rotation = {c, -s, 0, s, c, 0, 0, 0, 1};
    }
    auto graph = FrameGraph::build(definitions, 33);
    REQUIRE(graph.ok());
    auto frame = graph.value().frame(32);
    REQUIRE(frame.ok());
    for (int sample = 1; sample <= 30; ++sample) {
        CellPosition               point{{sample, 2 * sample, -sample}, {0.125, -0.25, 0.375}};
        std::array<long double, 3> expected{sample + 0.125L, 2 * sample - 0.25L, -sample + 0.375L};
        for (std::size_t i = 32; i > 0; --i) {
            std::array<long double, 3> next{};
            for (unsigned row = 0; row < 3; ++row)
                for (unsigned k = 0; k < 3; ++k)
                    next[row] += static_cast<long double>(definitions[i].rotation[row * 3 + k]) * expected[k];
            expected = next;
        }
        auto result = graph.value().project({frame.value(), point}, {frame.value(), {}}, 0, 1e-8);
        REQUIRE(result.ok());
        for (unsigned axis = 0; axis < 3; ++axis)
            REQUIRE(std::abs(static_cast<long double>(result.value().offset[axis]) - expected[axis]) <=
                    result.value().absoluteErrorBound[axis]);
    }
    auto rejected = graph.value().project({frame.value(), {{1, 2, 3}, {}}}, {frame.value(), {}}, 0, 1e-30);
    REQUIRE(!rejected.ok());
}
TEST_CASE("spatial.frameGraph.rejectsReflectionShearNonfiniteAndRotatedRoot") {
    std::vector<FrameDefinition> definitions(2);
    for (const auto& invalid :
         std::vector<std::array<double, 9>>{{-1, 0, 0, 0, 1, 0, 0, 0, 1},
                                            {1, 0.01, 0, 0, 1, 0, 0, 0, 1},
                                            {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 1, 0, 0, 0, 1}}) {
        definitions[1].rotation = invalid;
        auto graph              = FrameGraph::build(definitions, 2);
        REQUIRE(!graph.ok());
    }
    definitions[1]          = {};
    definitions[0].rotation = quarterZ;
    auto root               = FrameGraph::build(definitions, 2);
    REQUIRE(!root.ok());
}

TEST_CASE("spatial.frameGraph.exactRotatedZeroSurvivesExtremeScaleAndMinimalBudget") {
    std::vector<FrameDefinition> definitions(2);
    definitions[1].exponent = 900;
    definitions[1].rotation = quarterZ;
    auto graph              = FrameGraph::build(definitions, 2);
    REQUIRE(graph.ok());
    auto frame = graph.value().frame(1);
    REQUIRE(frame.ok());
    CellPosition point{{INT64_MAX, INT64_MIN, 0}, {0.125, -0.25, 0}};
    auto         zero = graph.value().project({frame.value(), point}, {frame.value(), point}, -900,
                                              std::numeric_limits<double>::denorm_min());
    REQUIRE(zero.ok());
    for (unsigned axis = 0; axis < 3; ++axis) {
        REQUIRE_EQ(zero.value().offset[axis], 0.0);
        REQUIRE_EQ(zero.value().absoluteErrorBound[axis], 0.0);
    }
}
TEST_CASE("spatial.frameGraph.exactQuarterTurnsDoNotAccumulateArtificialOrientationError") {
    std::vector<FrameDefinition> definitions(65);
    for (std::size_t i = 1; i < definitions.size(); ++i) {
        definitions[i].parentIndex = i - 1;
        definitions[i].rotation    = quarterZ;
    }
    auto graph = FrameGraph::build(definitions, 65);
    REQUIRE(graph.ok());
    auto frame = graph.value().frame(64);
    REQUIRE(frame.ok());
    CellPosition point{{1, 2, 3}, {}};
    auto         direct = projectCellRelative(point, {}, {0, 0, 1e-12});
    REQUIRE(direct.ok());
    auto rotated = graph.value().project({frame.value(), point}, {frame.value(), {}}, 0, 1e-12);
    REQUIRE(rotated.ok());
    for (unsigned axis = 0; axis < 3; ++axis) {
        REQUIRE(std::abs(rotated.value().offset[axis] - direct.value().offset[axis]) <=
                rotated.value().absoluteErrorBound[axis]);
        REQUIRE(rotated.value().absoluteErrorBound[axis] <= 2 * direct.value().absoluteErrorBound[axis]);
    }
}
TEST_CASE("spatial.frameGraph.reexpressCancelsHugeAncestorAndInvertsDestinationAxes") {
    std::vector<FrameDefinition> definitions(4);
    definitions[1].translation.cell = {INT64_MAX, INT64_MIN, 0};
    definitions[1].exponent         = 800;
    definitions[2]                  = {1, {{10, 0, 0}, {}}, 2};
    definitions[3]                  = {1, {{-5, 3, 1}, {}}, -1};
    definitions[3].rotation         = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    auto graph                      = FrameGraph::build(definitions, 4);
    REQUIRE(graph.ok());
    auto source = graph.value().frame(2), destination = graph.value().frame(3);
    REQUIRE(source.ok());
    REQUIRE(destination.ok());
    auto result = graph.value().reexpress({source.value(), {{1, 2, 3}, {}}}, destination.value(), 1e-9);
    REQUIRE(result.ok());
    REQUIRE(result.value().position.frame == destination.value());
    const std::array<double, 3> expected{10, -38, 22};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto&  position = result.value().position.position;
        const double actual   = static_cast<double>(position.cell[axis]) + position.fraction[axis];
        REQUIRE(std::abs(actual - expected[axis]) <= result.value().absoluteErrorBound[axis]);
        REQUIRE(result.value().absoluteErrorBound[axis] <= 1e-9);
    }
}

TEST_CASE("spatial.frameGraph.reexpressSameFramePreservesFullWidthAddresses") {
    std::vector<FrameDefinition> definitions(2);
    definitions[1].exponent = 900;
    auto graph              = FrameGraph::build(definitions, 2);
    REQUIRE(graph.ok());
    auto frame = graph.value().frame(1);
    REQUIRE(frame.ok());
    const CellPosition position{{INT64_MAX, INT64_MIN, 7}, {0.125, -0.25, 0.375}};
    auto               result = graph.value().reexpress({frame.value(), position}, frame.value(), 1e-300);
    REQUIRE(result.ok());
    REQUIRE(result.value().position.position.cell == position.cell);
    REQUIRE(result.value().position.position.fraction == position.fraction);
    for (auto bound : result.value().absoluteErrorBound) REQUIRE_EQ(bound, 0.0);
}

TEST_CASE("spatial.frameGraph.reexpressUsesRepresentedInverseNotTranspose") {
    std::vector<FrameDefinition> definitions(2);
    const double                 representedScale = 1 + std::ldexp(1.0, -43);
    definitions[1].rotation[0]                    = representedScale;
    auto graph                                    = FrameGraph::build(definitions, 2);
    REQUIRE(graph.ok());
    auto root = graph.value().frame(0), destination = graph.value().frame(1);
    REQUIRE(root.ok());
    REQUIRE(destination.ok());
    auto result = graph.value().reexpress({root.value(), {{1000000, 0, 0}, {}}}, destination.value(), 1e-7);
    REQUIRE(result.ok());
    const auto&  position = result.value().position.position;
    const double actual   = static_cast<double>(position.cell[0]) + position.fraction[0];
    REQUIRE(std::abs(actual - 1000000.0 / representedScale) <= result.value().absoluteErrorBound[0]);
    REQUIRE(std::abs(actual - 1000000.0 * representedScale) > result.value().absoluteErrorBound[0]);
}

TEST_CASE("spatial.frameGraph.reexpressGeneralOrientationBoundsForwardResidual") {
    for (double angle : {-2.1, -0.37, 0.21, 1.7}) {
        std::vector<FrameDefinition> definitions(2);
        const std::array<double, 3>  axis{1 / std::sqrt(14.0), 2 / std::sqrt(14.0), 3 / std::sqrt(14.0)};
        const double                 cosine = std::cos(angle), sine = std::sin(angle);
        auto&                        matrix = definitions[1].rotation;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col)
                matrix[row * 3 + col] = (row == col ? cosine : 0) + (1 - cosine) * axis[row] * axis[col];
        matrix[1] -= sine * axis[2];
        matrix[2] += sine * axis[1];
        matrix[3] += sine * axis[2];
        matrix[5] -= sine * axis[0];
        matrix[6] -= sine * axis[1];
        matrix[7] += sine * axis[0];
        definitions[1].translation.cell = {9, 0, -3};
        definitions[1].exponent         = 3;
        auto graph                      = FrameGraph::build(definitions, 2);
        REQUIRE(graph.ok());
        auto root = graph.value().frame(0), destination = graph.value().frame(1);
        REQUIRE(root.ok());
        REQUIRE(destination.ok());
        const FramePosition source{root.value(), {{3, -2, 4}, {}}};
        auto                result = graph.value().reexpress(source, destination.value(), 1e-10);
        REQUIRE(result.ok());
        REQUIRE(!graph.value().reexpress(source, destination.value(), 1e-30).ok());
        for (unsigned row = 0; row < 3; ++row) {
            double reconstructed = static_cast<double>(definitions[1].translation.cell[row]);
            double error         = 0;
            for (unsigned col = 0; col < 3; ++col) {
                const auto&  position = result.value().position.position;
                const double local    = static_cast<double>(position.cell[col]) + position.fraction[col];
                reconstructed += 8 * matrix[row * 3 + col] * local;
                error += 8 * std::abs(matrix[row * 3 + col]) * result.value().absoluteErrorBound[col];
            }
            // Independent forward evaluation has its own binary64 rounding.
            REQUIRE(std::abs(reconstructed - static_cast<double>(source.position.cell[row])) <= error + 1e-13);
        }
    }
}

TEST_CASE("spatial.frameGraph.reexpressRejectsForeignMovedInvalidAndOutOfRange") {
    std::vector<FrameDefinition> definitions(2);
    definitions[1].exponent = -1;
    auto graph              = FrameGraph::build(definitions, 2);
    auto foreign            = FrameGraph::build(definitions, 2);
    REQUIRE(graph.ok());
    REQUIRE(foreign.ok());
    auto root = graph.value().frame(0), destination = graph.value().frame(1), other = foreign.value().frame(0);
    REQUIRE(root.ok());
    REQUIRE(destination.ok());
    REQUIRE(other.ok());
    REQUIRE(!graph.value().reexpress({root.value(), {}}, other.value(), 1e-9).ok());
    REQUIRE(!graph.value().reexpress({other.value(), {}}, root.value(), 1e-9).ok());
    REQUIRE(!graph.value().reexpress({root.value(), {}}, root.value(), 0).ok());
    CellPosition invalid;
    invalid.fraction[0] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!graph.value().reexpress({root.value(), invalid}, root.value(), 1e-9).ok());
    REQUIRE(!graph.value().reexpress({root.value(), {{INT64_MAX, 0, 0}, {}}}, destination.value(), 1e5).ok());
    auto moved = std::move(graph.value());
    REQUIRE(!graph.value().reexpress({root.value(), {}}, destination.value(), 1e-9).ok());
    REQUIRE(moved.reexpress({root.value(), {}}, destination.value(), 1e-9).ok());
}
