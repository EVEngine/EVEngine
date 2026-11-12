#include "physics/field/GravityField.h"
#include "physics/field/GravitySimulation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace eve::physics::field;

namespace {
FieldStamp stamp() { return {eve::SimulationTick{17}, eve::Revision{4}}; }

std::vector<Particle> clusters() {
    std::vector<Particle> result;
    for (int cluster = 0; cluster < 8; ++cluster)
        for (int i = 0; i < 8; ++i)
            result.push_back(
                {{(cluster & 1 ? 20.0 : -20.0) + 0.1 * (i & 1), (cluster & 2 ? 20.0 : -20.0) + 0.1 * (i & 2),
                  (cluster & 4 ? 20.0 : -20.0) + 0.1 * (i & 4)},
                 1.0 + 0.03 * i});
    return result;
}

GravitySample direct(const std::vector<Particle>& points, std::size_t target, double g) {
    GravitySample sample;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i == target) continue;
        std::array<double, 3> d{};
        for (unsigned axis = 0; axis < 3; ++axis) d[axis] = points[i].position[axis] - points[target].position[axis];
        const double r = std::hypot(d[0], d[1], d[2]);
        sample.potential -= g * points[i].mass / r;
        for (unsigned axis = 0; axis < 3; ++axis)
            sample.acceleration[axis] += g * points[i].mass * d[axis] / (r * r * r);
    }
    return sample;
}
}  // namespace

TEST_CASE("physics.field.sharedTargetBudgetBoundsMixedDepthInteractions") {
    std::vector<Particle> points;
    for (unsigned i = 0; i < 384; ++i) {
        const double cluster = i < 192 ? 0.02 : 1.0;
        points.push_back({{{cluster * (static_cast<double>((i * 37) % 389) / 389 - 0.5),
                            cluster * (static_cast<double>((i * 71) % 397) / 397 - 0.5),
                            cluster * (static_cast<double>((i * 97) % 401) / 401 - 0.5)}},
                          (1 + i % 13) / 2688.0});
    }
    GravityPolicy policy;
    policy.leafCapacity              = 4;
    policy.absoluteAccelerationError = 0.1;
    for (unsigned order : {2u, 4u, 6u}) {
        policy.expansionOrder = order;
        auto field            = GravityField::build(points, stamp(), policy);
        REQUIRE(field.ok());
        REQUIRE(field.value().metrics().farInteractions > 0);
        auto samples = field.value().evaluate(stamp());
        REQUIRE(samples.ok());
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto   reference = direct(points, i, policy.gravitationalConstant);
            const auto&  sample    = samples.value()[i];
            const double error     = std::hypot(sample.acceleration[0] - reference.acceleration[0],
                                                sample.acceleration[1] - reference.acceleration[1],
                                                sample.acceleration[2] - reference.acceleration[2]);
            const double roundoffAllowance =
                1e-10 * std::max(1.0, std::hypot(reference.acceleration[0], reference.acceleration[1],
                                                 reference.acceleration[2]));
            REQUIRE(sample.truncationErrorBound <= policy.absoluteAccelerationError);
            REQUIRE(error <= sample.truncationErrorBound + roundoffAllowance);
        }
    }
}

TEST_CASE("physics.field.twoBodyHasCorrectSignUnitsAndNoSelfForce") {
    std::vector<Particle> points{{{0, 0, 0}, 2}, {{3, 0, 0}, 5}};
    GravityPolicy         policy;
    policy.gravitationalConstant = 2.0;
    auto field                   = GravityField::build(points, stamp(), policy);
    REQUIRE(field.ok());
    auto evaluated = field.value().evaluate(stamp());
    REQUIRE(evaluated.ok());
    REQUIRE(std::abs(evaluated.value()[0].acceleration[0] - 10.0 / 9.0) < 1e-14);
    REQUIRE(std::abs(evaluated.value()[1].acceleration[0] + 4.0 / 9.0) < 1e-14);
    REQUIRE(std::abs(evaluated.value()[0].potential + 10.0 / 3.0) < 1e-14);
    REQUIRE_EQ(field.value().metrics().directPairs, std::size_t{2});
}

TEST_CASE("physics.field.independentTargetsMatchDirectAcrossOrdersAndExclusions") {
    const auto               points = clusters();
    std::vector<FieldTarget> targets;
    for (unsigned i = 0; i < 17; ++i) targets.push_back({{70.0 + i, -40.0 + 0.3 * i, 30.0 - 0.2 * i}, {}});
    targets.push_back({points[3].position, 3});
    targets.push_back({points[3].position, 3});
    for (unsigned order = 2; order <= 6; ++order) {
        GravityPolicy policy;
        policy.expansionOrder            = order;
        policy.leafCapacity              = 4;
        policy.absoluteAccelerationError = 1e-4;
        auto field                       = GravityField::buildAtTargets(points, targets, stamp(), policy);
        REQUIRE(field.ok());
        REQUIRE_EQ(field.value().metrics().particles, points.size());
        REQUIRE_EQ(field.value().metrics().targets, targets.size());
        REQUIRE(field.value().metrics().farInteractions > 0);
        auto samples = field.value().evaluate(stamp());
        REQUIRE(samples.ok());
        REQUIRE_EQ(samples.value().size(), targets.size());
        for (std::size_t target = 0; target < targets.size(); ++target) {
            auto referencePoints = points;
            // Omit an excluded coincident row rather than evaluating zero divided by zero.
            if (targets[target].excludedSource)
                referencePoints.erase(referencePoints.begin() +
                                      static_cast<std::ptrdiff_t>(*targets[target].excludedSource));
            referencePoints.push_back({targets[target].position, 0});
            const auto   reference = direct(referencePoints, referencePoints.size() - 1, policy.gravitationalConstant);
            const auto&  sample    = samples.value()[target];
            const double error     = std::hypot(sample.acceleration[0] - reference.acceleration[0],
                                                sample.acceleration[1] - reference.acceleration[1],
                                                sample.acceleration[2] - reference.acceleration[2]);
            const double allowance =
                1e-10 * std::max(1.0, std::hypot(reference.acceleration[0], reference.acceleration[1],
                                                 reference.acceleration[2]));
            REQUIRE(sample.truncationErrorBound <= policy.absoluteAccelerationError);
            REQUIRE(error <= sample.truncationErrorBound + allowance);
        }
    }
}

TEST_CASE("physics.field.independentTargetsOwnInputsAndPreserveTargetOrder") {
    std::vector<Particle>    points{{{0, 0, 0}, 2}};
    std::vector<FieldTarget> targets{{{2, 0, 0}, {}}, {{-4, 0, 0}, {}}, {{2, 0, 0}, {}}};
    auto                     field = GravityField::buildAtTargets(points, targets, stamp(), GravityPolicy{});
    REQUIRE(field.ok());
    points[0].mass      = 100;
    targets[0].position = {0, 0, 0};
    auto samples        = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    REQUIRE_EQ(samples.value().size(), 3u);
    // The common normalization includes thirds; the analytic values need not be bit-exact.
    const double roundoff = 16 * std::numeric_limits<double>::epsilon();
    REQUIRE(std::abs(samples.value()[0].acceleration[0] + 0.5) <= roundoff);
    REQUIRE(std::abs(samples.value()[1].acceleration[0] - 0.125) <= roundoff);
    REQUIRE_EQ(samples.value()[2].acceleration[0], samples.value()[0].acceleration[0]);
    REQUIRE(std::abs(samples.value()[0].potential + 1.0) <= roundoff);
    REQUIRE(std::abs(samples.value()[1].potential + 0.5) <= roundoff);
    REQUIRE_EQ(field.value().metrics().directPairs, 3u);
    auto wrong     = stamp();
    wrong.revision = eve::Revision{99};
    REQUIRE(!field.value().evaluate(wrong).ok());
}

TEST_CASE("physics.field.independentTargetsExcludeSelfWithoutSpendingDirectBudget") {
    const std::vector<Particle>    points{{{1, 2, 3}, 5}};
    const std::vector<FieldTarget> targets{{points[0].position, 0}};
    GravityPolicy                  policy;
    policy.maxDirectPairs = 0;
    auto field            = GravityField::buildAtTargets(points, targets, stamp(), policy);
    REQUIRE(field.ok());
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    REQUIRE_EQ(field.value().metrics().directPairs, 0u);
    REQUIRE_EQ(samples.value()[0].potential, 0.0);
    for (auto value : samples.value()[0].acceleration) REQUIRE_EQ(value, 0.0);
    auto empty = GravityField::buildAtTargets(points, {}, stamp(), policy);
    REQUIRE(empty.ok());
    auto emptySamples = empty.value().evaluate(stamp());
    REQUIRE(emptySamples.ok());
    REQUIRE(emptySamples.value().empty());
    REQUIRE_EQ(empty.value().metrics().targets, 0u);
    REQUIRE_EQ(empty.value().metrics().traversalPairs, 0u);
}

TEST_CASE("physics.field.independentTargetsValidateExclusionsSingularityAndCombinedBudget") {
    const std::vector<Particle> points{{{0, 0, 0}, 1}};
    std::vector<FieldTarget>    targets{{{1, 0, 0}, 0}};
    REQUIRE(!GravityField::buildAtTargets(points, targets, stamp(), GravityPolicy{}).ok());
    targets[0] = {{0, 0, 0}, 1};
    REQUIRE(!GravityField::buildAtTargets(points, targets, stamp(), GravityPolicy{}).ok());
    targets[0] = {{std::numeric_limits<double>::infinity(), 0, 0}, {}};
    REQUIRE(!GravityField::buildAtTargets(points, targets, stamp(), GravityPolicy{}).ok());
    targets[0]    = {{0, 0, 0}, {}};
    auto singular = GravityField::buildAtTargets(points, targets, stamp(), GravityPolicy{});
    REQUIRE(singular.ok());
    REQUIRE(!singular.value().evaluate(stamp()).ok());
    GravityPolicy policy;
    policy.maxParticles = 1;
    REQUIRE(!GravityField::buildAtTargets(points, targets, stamp(), policy).ok());
    REQUIRE(!GravityField::buildAtTargets({}, targets, stamp(), GravityPolicy{}).ok());
}

TEST_CASE("physics.field.independentTargetsCountExcludedPairsAfterSpatialReordering") {
    const std::vector<Particle>    points{{{2, 0, 0}, 1}, {{-1, 0, 0}, 2}, {{0, 1, 0}, 3}};
    const std::vector<FieldTarget> targets{{points[1].position, 1}, {points[2].position, 2}, {points[0].position, 0}};
    GravityPolicy                  policy;
    policy.leafCapacity   = 1;
    policy.openingRatio   = 1e-12;
    policy.maxDirectPairs = 6;
    auto field            = GravityField::buildAtTargets(points, targets, stamp(), policy);
    REQUIRE(field.ok());
    REQUIRE_EQ(field.value().metrics().directPairs, 6u);
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    for (std::size_t target = 0; target < targets.size(); ++target) {
        const auto reference = direct(points, *targets[target].excludedSource, 1);
        for (unsigned axis = 0; axis < 3; ++axis)
            REQUIRE(std::abs(samples.value()[target].acceleration[axis] - reference.acceleration[axis]) <= 1e-13);
    }
    policy.maxDirectPairs = 5;
    REQUIRE(!GravityField::buildAtTargets(points, targets, stamp(), policy).ok());
}

TEST_CASE("physics.field.allTaylorOrdersMatchDirectForSignedAxesAndPhysicalScales") {
    for (unsigned order = 2; order <= 6; ++order) {
        for (unsigned permutation = 0; permutation < 3; ++permutation) {
            for (int sign : {-1, 1}) {
                for (int exponent : {-40, 0, 40}) {
                    auto points = clusters();
                    for (auto& point : points) {
                        const auto original = point.position;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            point.position[axis] = std::ldexp(original[(axis + permutation) % 3] * sign, exponent);
                        point.mass = std::ldexp(point.mass, 2 * exponent);
                    }
                    GravityPolicy policy;
                    policy.expansionOrder            = order;
                    policy.leafCapacity              = 4;
                    policy.absoluteAccelerationError = 1e-4;
                    auto field                       = GravityField::build(points, stamp(), policy);
                    REQUIRE(field.ok());
                    REQUIRE(field.value().metrics().farInteractions > 0);
                    REQUIRE_EQ(field.value().metrics().coefficientCount,
                               static_cast<std::size_t>((order + 1) * (order + 2) * (order + 3) / 6));
                    auto samples = field.value().evaluate(stamp());
                    REQUIRE(samples.ok());
                    for (std::size_t target = 0; target < points.size(); ++target) {
                        const auto   reference = direct(points, target, policy.gravitationalConstant);
                        const auto&  sample    = samples.value()[target];
                        const double error     = std::hypot(sample.acceleration[0] - reference.acceleration[0],
                                                            sample.acceleration[1] - reference.acceleration[1],
                                                            sample.acceleration[2] - reference.acceleration[2]);
                        const double referenceNorm =
                            std::hypot(reference.acceleration[0], reference.acceleration[1], reference.acceleration[2]);
                        REQUIRE(sample.truncationErrorBound <= policy.absoluteAccelerationError);
                        // The public bound excludes roundoff; keep the independent allowance explicit.
                        REQUIRE(error <= sample.truncationErrorBound + 1e-10 * std::max(1.0, referenceNorm));
                    }
                }
            }
        }
    }
}

TEST_CASE("physics.field.tinyRepresentableSeparationSurvivesSquaredNormUnderflow") {
    for (int exponent : {-530, -600}) {
        const double          separation = std::ldexp(1.0, exponent);
        const double          mass       = std::ldexp(1.0, exponent == -530 ? -1060 : -1000);
        std::vector<Particle> points{{{0, 0, 0}, mass}, {{0, separation, 0}, mass}, {{-1, 0, 0}, 1}, {{1, 0, 0}, 1}};
        GravityPolicy         policy;
        policy.gravitationalConstant = exponent == -530 ? 1.0 : std::ldexp(1.0, -300);
        auto field                   = GravityField::build(points, stamp(), policy);
        REQUIRE(field.ok());
        auto output = field.value().evaluate(stamp());
        REQUIRE(output.ok());
        const double expected = exponent == -530 ? 1.0 : std::ldexp(1.0, -100);
        REQUIRE(std::abs(output.value()[0].acceleration[1] - expected) <= expected * 1e-12);
        REQUIRE(std::abs(output.value()[1].acceleration[1] + expected) <= expected * 1e-12);
        REQUIRE_EQ(field.value().metrics().directPairs, std::size_t{12});
    }
}

TEST_CASE("physics.field.localExpansionsMatchDirectReferenceWithinBound") {
    auto          points = clusters();
    GravityPolicy policy;
    policy.leafCapacity              = 4;
    policy.absoluteAccelerationError = 1e-3;
    for (unsigned order : {2u, 4u, 6u}) {
        policy.expansionOrder = order;
        auto field            = GravityField::build(points, stamp(), policy);
        REQUIRE(field.ok());
        REQUIRE(field.value().metrics().farInteractions > 0);
        REQUIRE(field.value().metrics().directPairs < points.size() * (points.size() - 1));
        auto evaluated = field.value().evaluate(stamp());
        REQUIRE(evaluated.ok());
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto   reference = direct(points, i, policy.gravitationalConstant);
            const auto&  actual    = evaluated.value()[i];
            const double error     = std::hypot(actual.acceleration[0] - reference.acceleration[0],
                                                actual.acceleration[1] - reference.acceleration[1],
                                                actual.acceleration[2] - reference.acceleration[2]);
            REQUIRE(error <= actual.truncationErrorBound + 1e-9);
            REQUIRE(actual.truncationErrorBound <= policy.absoluteAccelerationError * (1.0 + 1e-12));
        }
    }
}

TEST_CASE("physics.field.massiveDistantSourceHasNoRangeCutoff") {
    std::vector<Particle> points{{{0, 0, 0}, 1}, {{1e100, 0, 0}, 1e200}};
    auto                  field = GravityField::build(points, stamp(), {});
    REQUIRE(field.ok());
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    REQUIRE(std::abs(samples.value()[0].acceleration[0] - 1.0) < 1e-14);
    REQUIRE(std::abs(samples.value()[1].acceleration[0] / -1e-200 - 1.0) < 1e-14);
    REQUIRE(std::abs(samples.value()[0].potential / -1e100 - 1.0) < 1e-14);
}

TEST_CASE("physics.field.symmetricCancellationUsesAbsoluteError") {
    std::vector<Particle> points{{{0, 0, 0}, 1}};
    for (unsigned axis = 0; axis < 3; ++axis) {
        Particle positive{{0, 0, 0}, 2}, negative = positive;
        positive.position[axis] = 10;
        negative.position[axis] = -10;
        points.push_back(positive);
        points.push_back(negative);
    }
    GravityPolicy policy;
    policy.leafCapacity = 1;
    auto field          = GravityField::build(points, stamp(), policy);
    REQUIRE(field.ok());
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    const auto& center = samples.value()[0];
    REQUIRE(std::hypot(center.acceleration[0], center.acceleration[1], center.acceleration[2]) < 1e-15);
    REQUIRE(std::abs(center.potential + 1.2) < 1e-14);
}

TEST_CASE("physics.field.permutationAndTranslationPreserveForceWithinTolerance") {
    auto          points = clusters();
    GravityPolicy policy;
    policy.leafCapacity              = 4;
    policy.absoluteAccelerationError = 1e-3;
    auto field                       = GravityField::build(points, stamp(), policy);
    REQUIRE(field.ok());
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    std::reverse(points.begin(), points.end());
    for (auto& point : points)
        for (auto& axis : point.position) axis += 1024;
    auto moved = GravityField::build(points, stamp(), policy);
    REQUIRE(moved.ok());
    auto movedSamples = moved.value().evaluate(stamp());
    REQUIRE(movedSamples.ok());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& before = samples.value()[points.size() - 1 - i];
        const auto& after  = movedSamples.value()[i];
        for (unsigned axis = 0; axis < 3; ++axis)
            REQUIRE(std::abs(before.acceleration[axis] - after.acceleration[axis]) <
                    before.truncationErrorBound + after.truncationErrorBound + 1e-8);
    }
}

TEST_CASE("physics.field.snapshotOwnsInputAndRejectsStaleStamp") {
    auto points = clusters();
    auto field  = GravityField::build(points, stamp(), {});
    REQUIRE(field.ok());
    points.clear();
    auto stale = field.value().evaluate({eve::SimulationTick{18}, eve::Revision{4}});
    REQUIRE(!stale.ok());
    REQUIRE(stale.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    auto good = field.value().evaluate(stamp());
    REQUIRE(good.ok());
    REQUIRE_EQ(good.value().size(), std::size_t{64});
    auto moved   = std::move(field.value());
    auto invalid = field.value().evaluate(stamp());
    REQUIRE(!invalid.ok());
    auto valid = moved.evaluate(stamp());
    REQUIRE(valid.ok());
}

TEST_CASE("physics.field.rejectsInvalidSourcesAndSingularCoincidence") {
    std::vector<Particle> points{{{0, 0, 0}, 1}, {{0, 0, 0}, 2}};
    auto                  field = GravityField::build(points, stamp(), {});
    REQUIRE(field.ok());
    auto singular = field.value().evaluate(stamp());
    REQUIRE(!singular.ok());
    REQUIRE(singular.status().primaryDiagnostic()->code() == eve::DiagnosticCode::InvalidArgument);
    points[1].mass = -1;
    auto negative  = GravityField::build(points, stamp(), {});
    REQUIRE(!negative.ok());
    points[1].mass        = 1;
    points[1].position[0] = std::numeric_limits<double>::infinity();
    auto infinite         = GravityField::build(points, stamp(), {});
    REQUIRE(!infinite.ok());
}

TEST_CASE("physics.field.budgetsFailBeforeUnboundedDirectEvaluation") {
    auto          points = clusters();
    GravityPolicy policy;
    policy.maxNodes = 1;
    auto nodes      = GravityField::build(points, stamp(), policy);
    REQUIRE(!nodes.ok());
    policy.maxNodes        = 10000;
    policy.maxInteractions = 1;
    auto interactions      = GravityField::build(points, stamp(), policy);
    REQUIRE(!interactions.ok());
    policy.maxInteractions = 10000;
    policy.leafCapacity    = 100;
    policy.maxDirectPairs  = 1;
    auto directPairs       = GravityField::build(points, stamp(), policy);
    REQUIRE(!directPairs.ok());
}

TEST_CASE("physics.field.singleParticleIsAnExactZeroField") {
    std::vector<Particle> points{{{1e100, -1e100, 1e100}, 1e100}};
    auto                  field = GravityField::build(points, stamp(), {});
    REQUIRE(field.ok());
    auto output = field.value().evaluate(stamp());
    REQUIRE(output.ok());
    REQUIRE_EQ(output.value()[0].potential, 0.0);
    REQUIRE_EQ(output.value()[0].acceleration[0], 0.0);
    REQUIRE_EQ(output.value()[0].truncationErrorBound, 0.0);
}

TEST_CASE("physics.field.unaryPathsAreCompressedWithoutLosingSources") {
    std::vector<Particle> points{{{0, 0, 0}, 1}, {{1e-8, 0, 0}, 1}, {{1, 0, 0}, 1}};
    GravityPolicy         policy;
    policy.leafCapacity = 1;
    policy.maxDepth     = 40;
    policy.maxNodes     = 5;
    auto field          = GravityField::build(points, stamp(), policy);
    REQUIRE(field.ok());
    REQUIRE_EQ(field.value().metrics().nodes, std::size_t{5});
    REQUIRE(field.value().metrics().treeDepth > 20);
    auto samples = field.value().evaluate(stamp());
    REQUIRE(samples.ok());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto reference = direct(points, i, 1.0);
        REQUIRE(std::abs(samples.value()[i].acceleration[0] - reference.acceleration[0]) <
                1e-7 * std::abs(reference.acceleration[0]));
    }
}

TEST_CASE("physics.field.simulationPublishesOnlyCurrentOwnerCandidate") {
    const auto               dt = eve::Duration::fromNanoseconds(10000000);
    std::vector<MotionState> initial{{{{-1, 0, 0}, 1}, {0, -0.5, 0}}, {{{1, 0, 0}, 1}, {0, 0.5, 0}}};
    auto                     first  = GravitySimulation::create(initial, stamp(), {}, dt);
    auto                     second = GravitySimulation::create(initial, stamp(), {}, dt);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const eve::SimulationStep next{eve::SimulationTick{18}, dt};
    auto                      candidate = first.value().prepareStep(next);
    REQUIRE(candidate.ok());
    auto foreign = second.value().commit(std::move(candidate.value()));
    REQUIRE(!foreign.ok());
    auto accepted   = first.value().prepareStep(next);
    auto superseded = first.value().prepareStep(next);
    REQUIRE(accepted.ok());
    REQUIRE(superseded.ok());
    auto before = first.value().snapshot();
    REQUIRE(before.ok());
    REQUIRE_EQ(before.value().states[0].particle.position[0], -1.0);
    auto commit = first.value().commit(std::move(accepted.value()));
    REQUIRE(commit.ok());
    auto stale = first.value().commit(std::move(superseded.value()));
    REQUIRE(!stale.ok());
    auto after = first.value().snapshot();
    REQUIRE(after.ok());
    REQUIRE(after.value().stamp.tick == next.tick);
    REQUIRE(after.value().states[0].particle.position[0] > -1.0);
    auto repeated = first.value().prepareStep(next);
    REQUIRE(!repeated.ok());
    auto wrongDt = first.value().prepareStep({eve::SimulationTick{19}, eve::Duration::fromNanoseconds(1)});
    REQUIRE(!wrongDt.ok());
}

TEST_CASE("physics.field.failedStepPreservesPositionVelocityAndTick") {
    const auto               dt = eve::Duration::fromNanoseconds(1000000000);
    std::vector<MotionState> initial{{{{-1, 0, 0}, 1}, {0.875, 0, 0}}, {{{1, 0, 0}, 1}, {-0.875, 0, 0}}};
    auto                     world = GravitySimulation::create(initial, stamp(), {}, dt);
    REQUIRE(world.ok());
    auto candidate = world.value().prepareStep({eve::SimulationTick{18}, dt});
    REQUIRE(!candidate.ok());
    auto snapshot = world.value().snapshot();
    REQUIRE(snapshot.ok());
    REQUIRE(snapshot.value().stamp == stamp());
    REQUIRE_EQ(snapshot.value().states[0].particle.position[0], -1.0);
    REQUIRE_EQ(snapshot.value().states[0].velocity[0], 0.875);
}

TEST_CASE("physics.field.verletOrbitControlsEnergyAndMomentumDrift") {
    const auto               dt = eve::Duration::fromNanoseconds(10000000);
    std::vector<MotionState> initial{{{{-1, 0, 0}, 1}, {0, -0.5, 0}}, {{{1, 0, 0}, 1}, {0, 0.5, 0}}};
    auto                     world = GravitySimulation::create(initial, {}, {}, dt);
    REQUIRE(world.ok());
    for (std::uint64_t tick = 1; tick <= 2000; ++tick) {
        auto candidate = world.value().prepareStep({eve::SimulationTick{tick}, dt});
        REQUIRE(candidate.ok());
        auto committed = world.value().commit(std::move(candidate.value()));
        REQUIRE(committed.ok());
    }
    auto snapshot = world.value().snapshot();
    REQUIRE(snapshot.ok());
    const auto& a       = snapshot.value().states[0];
    const auto& b       = snapshot.value().states[1];
    double      kinetic = 0, momentum = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        kinetic += 0.5 * (a.velocity[axis] * a.velocity[axis] + b.velocity[axis] * b.velocity[axis]);
        momentum += std::abs(a.velocity[axis] + b.velocity[axis]);
    }
    const double distance =
        std::hypot(a.particle.position[0] - b.particle.position[0], a.particle.position[1] - b.particle.position[1],
                   a.particle.position[2] - b.particle.position[2]);
    REQUIRE(std::abs(kinetic - 1.0 / distance + 0.25) < 1e-8);
    REQUIRE(momentum < 1e-12);
    const double angular = a.particle.position[0] * a.velocity[1] - a.particle.position[1] * a.velocity[0] +
                           b.particle.position[0] * b.velocity[1] - b.particle.position[1] * b.velocity[0];
    REQUIRE(std::abs(angular - 1.0) < 1e-12);
}
