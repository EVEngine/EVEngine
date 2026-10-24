#include "common/Value.h"
#include "procgen/road/RoadBake.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using eve::Value;
using namespace eve::procgen;
using namespace eve::procgen::road;

template <class T>
T require(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}

void require(eve::Result<void> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
}

Value vec(float x, float y, float z) { return Value::Array{x, y, z}; }

void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << content;
}

Value meshValue(const MeshBuild& mesh) {
    Value::Array positions, normals, uvs, indices;
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        positions.emplace_back(mesh.getPositionX(i));
        positions.emplace_back(mesh.getPositionY(i));
        positions.emplace_back(mesh.getPositionZ(i));
        normals.emplace_back(mesh.getNormalX(i));
        normals.emplace_back(mesh.getNormalY(i));
        normals.emplace_back(mesh.getNormalZ(i));
        uvs.emplace_back(mesh.getUvU(i));
        uvs.emplace_back(mesh.getUvV(i));
    }
    for (int i = 0; i < mesh.getIndexCount(); ++i) indices.emplace_back(mesh.getIndex(i));
    return Value::Object{{"positions", std::move(positions)},
                         {"normals", std::move(normals)},
                         {"uvs", std::move(uvs)},
                         {"indices", std::move(indices)}};
}

RoadNetwork fixture(const std::string& name) {
    std::vector<RoadControlPoint> points;
    if (name.rfind("sampling-", 0) == 0) points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    if (name == "s-curve") points = {{-18, 0, 0}, {-9, 0, 5}, {0, 0, 0}, {9, 0, -5}, {18, 0, 0}};
    if (name == "crest") points = {{-18, 0, 0}, {0, 5, 0}, {18, 0, 0}};
    if (name == "valley") points = {{-18, 5, 0}, {0, 0, 0}, {18, 5, 0}};
    if (name == "curved-uphill") points = {{-18, 0, 0}, {0, 3, 7}, {18, 6, 0}};
    if (name == "variable-bank") points = {{-18, 0, 0, 1, -10}, {0, 0, 0, 1, 15}, {18, 0, 0, 1, -10}};
    if (name == "negative-bank") points = {{-18, 0, 0, 1, -15}, {0, 0, 0, 1, -15}, {18, 0, 0, 1, -15}};
    if (name == "full-roll-positive" || name == "full-roll-negative")
        points = {{-18, 0, 0}, {0, 0, 0, 1, name == "full-roll-positive" ? 270.f : -270.f}, {18, 0, 0}};
    if (name == "bank-pivot-left" || name == "bank-pivot-right" || name == "bank-pivot-varying") {
        points = {{-18, 0, 0, 1, -10}, {0, 0, 0, 1, 15}, {18, 0, 0, 1, -10}};
        for (auto& point : points) point.rollCenterOffset = name == "bank-pivot-left" ? -.5f : .5f;
        if (name == "bank-pivot-varying") {
            points[0].rollCenterOffset = -.5f;
            points[1].rollCenterOffset = .5f;
            points[2].rollCenterOffset = 0.f;
        }
    }
    if (name == "curve-width") points = {{-18, 0, 0, .5f}, {0, 0, 7, 1.5f}, {18, 0, 0, 1}};
    if (name == "curve-width-bank") points = {{-18, 0, 0, .8f, 0}, {0, 0, 7, 1.3f, 10}, {18, 0, 0, 1, 0}};
    if (name == "strength-quarter" || name == "strength-full") {
        points                   = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
        points[1].splineStrength = name == "strength-quarter" ? .25f : 1.f;
    }
    if (name == "control-xz" || name == "control-xyz") {
        points                = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
        points[1].pathControl = name == "control-xz" ? RoadPathControl::StraightXZ : RoadPathControl::StraightXYZ;
    }
    if (name.rfind("control-circular", 0) == 0) {
        points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
        if (name.find("mirror") != std::string::npos)
            for (auto& p : points) p.z = -p.z;
        points[1].pathControl = RoadPathControl::Circular;
    }
    if (!points.empty()) {
        RoadNetwork network;
        const auto& first = points.front();
        const auto& last  = points.back();
        const auto  a     = require(network.addNode(first.x, first.y, first.z));
        const auto  b     = require(network.addNode(last.x, last.y, last.z));
        (void)require(network.addEdge(a, b, points, 2));
        return network;
    }
    if (name == "width-profile" || name == "banked") {
        RoadNetwork network;
        auto        a = require(network.addNode(-18.f, 0.f, 0.f)), b = require(network.addNode(18.f, 0.f, 0.f));
        const bool  width = name == "width-profile";
        (void)require(network.addEdge(a, b,
                                      {{-18.f, 0.f, 0.f, width ? .5f : 1.f, width ? 0.f : 15.f},
                                       {0.f, 0.f, 0.f, width ? 1.5f : 1.f, width ? 0.f : 15.f},
                                       {18.f, 0.f, 0.f, 1.f, width ? 0.f : 15.f}},
                                      2));
        return network;
    }
    if (name == "slope") {
        RoadNetwork network;
        auto        a = require(network.addNode(-18.f, 0.f, 0.f));
        auto        b = require(network.addNode(18.f, 6.f, 0.f));
        (void)require(network.addEdge(a, b, {{-18.f, 0.f, 0.f}, {0.f, 3.f, 0.f}, {18.f, 6.f, 0.f}}, 2));
        return network;
    }
    return require(RoadNetwork::makeScene(name, 36.f, 6.f, 2, 1));
}

void exportFixture(const std::filesystem::path& destination, const std::string& name) {
    auto network = fixture(name);
    // Match measured outer-package Default T/X Crossing sockets (5.5 metres),
    // rather than treating the engine's six-metre authoring default as prefab data.
    for (const auto& node : network.nodes()) {
        int degree = 0;
        for (const auto& edge : network.edges())
            if (edge.from == node.id || edge.to == node.id) ++degree;
        if (degree >= 3) require(network.setNodeJunctionRadius(node.id, 5.5f));
    }
    // Surface-only calibration: isolate centerline/junction geometry from
    // differing default barriers, sidewalks, deck meshes and material systems.
    const auto edges = network.edges();
    for (const auto& edge : edges) {
        auto style                  = edge.style;
        style.samplingSpacingMeters = 0.f;
        style.samplingPolicy        = RoadSamplingPolicy::MaximumIntervals;
        if (name == "full-roll-positive" || name == "full-roll-negative") {
            style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
            style.samplingSpacingMeters = .375f;
            style.samplingAngleDegrees  = 4.f;
        }
        if (name == "sampling-created-default") {
            style.samplingSpacingMeters = 2.f;
            style.samplingAngleDegrees  = 45.f;
            style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
        } else if (name.rfind("sampling-", 0) == 0) {
            style.samplingPolicy = RoadSamplingPolicy::ParametricDistanceTurn;
            style.samplingSpacingMeters =
                name == "sampling-resolution-1" || name == "sampling-transverse-3" ? 1.f : 10.f;
            style.samplingAngleDegrees = name == "sampling-angle-1" ? 1.f : name == "sampling-angle-20" ? 20.f : 4.f;
            style.crossSectionSubdivisions = name == "sampling-transverse-3" ? 3 : 1;
        }
        style.laneWidth = 3.f;
        style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
        style.deckThickness                                                             = 0.f;
        require(network.setEdgeStyle(edge.id, style));
    }
    Value::Array nodes, roads;
    for (const auto& node : network.nodes()) {
        nodes.emplace_back(Value::Object{{"id", static_cast<int>(node.id)},
                                         {"position", vec(node.x, node.y, node.z)},
                                         {"junctionRadius", node.junctionRadius}});
    }
    for (const auto& edge : network.edges()) {
        Value::Array markers;
        for (const auto& point : edge.controlPoints) markers.push_back(vec(point.x, point.y, point.z));
        Value::Object road{{"id", static_cast<int>(edge.id)},
                           {"from", static_cast<int>(edge.from)},
                           {"to", static_cast<int>(edge.to)},
                           {"width", edge.style.laneWidth * (edge.lanesForward + edge.lanesBackward)},
                           {"lanesForward", edge.lanesForward},
                           {"lanesBackward", edge.lanesBackward},
                           {"markers", std::move(markers)}};
        if (name.find("zero-bump") != std::string::npos) road.emplace("referenceZeroBumpiness", true);
        if (name == "sampling-created-default") road.emplace("referenceCreatedDefaults", true);
        if (edge.style.samplingSpacingMeters > 0.f) {
            road.emplace("resolution", edge.style.samplingSpacingMeters);
            road.emplace("angleThreshold", edge.style.samplingAngleDegrees);
            road.emplace("subSegments", edge.style.crossSectionSubdivisions);
        }
        bool hasProfiles = name == "width-profile" || name == "banked";
        for (const auto& point : edge.controlPoints)
            hasProfiles = hasProfiles || point.widthScale != 1.f || point.rollDegrees != 0.f;
        if (hasProfiles) {
            Value::Array scales, rolls;
            for (const auto& point : edge.controlPoints) {
                scales.emplace_back(point.widthScale);
                rolls.emplace_back(point.rollDegrees);
            }
            road.emplace("widthScales", std::move(scales));
            road.emplace("rollDegrees", std::move(rolls));
            bool hasCenters = false;
            for (const auto& point : edge.controlPoints) hasCenters = hasCenters || point.rollCenterOffset != 0.f;
            if (hasCenters) {
                Value::Array centers;
                for (const auto& point : edge.controlPoints) centers.emplace_back(point.rollCenterOffset + .5f);
                road.emplace("rollCenters", std::move(centers));
            }
        }
        bool hasStrength = false;
        for (const auto& point : edge.controlPoints) hasStrength = hasStrength || point.splineStrength != .5f;
        if (hasStrength) {
            Value::Array strengths;
            for (const auto& point : edge.controlPoints) strengths.emplace_back(point.splineStrength);
            road.emplace("strengths", std::move(strengths));
        }
        bool hasControl = false;
        for (const auto& point : edge.controlPoints)
            hasControl = hasControl || point.pathControl != RoadPathControl::Spline;
        if (hasControl) {
            Value::Array controls;
            for (const auto& point : edge.controlPoints) controls.emplace_back(static_cast<int>(point.pathControl));
            road.emplace("controls", std::move(controls));
        }
        roads.emplace_back(std::move(road));
    }
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 96;
    options.includePiers = options.includeMarkings = options.includeNavigation = options.includePlacements = false;
    auto                       bake = require(bakeRoadNetwork(network, options));
    std::unique_ptr<MeshBuild> surface;
    for (int group = 0; group < bake.mesh.getGroupCount(); ++group)
        if (bake.mesh.getGroupName(group) == "asphalt") surface = bake.mesh.copyGroup(group);
    if (!surface || surface->empty()) throw std::runtime_error("fixture produced no asphalt surface");

    const float elevation = name == "bridge" ? 6.f : name == "slope" ? 3.f : 0.f;
    Value       spec      = Value::Object{{"schema", "eve.tests.road-parity"},
                                          {"schemaVersion", 1},
                                          {"units", "metres"},
                                          {"axis", "Y-up"},
                                          {"caseId", name},
                                          {"scope", "surface-only"},
                                          {"width", 1280},
                                          {"height", 720},
                                          {"fovY", 42.f},
                                          {"near", 0.1f},
                                          {"far", 250.f},
                                          {"surfaceColor", vec(1.f, 0.f, 1.f)},
                                          {"backgroundColor", vec(0.125f, 0.125f, 0.125f)},
                                          {"perspectiveEye", vec(42.f, 36.f + elevation, 42.f)},
                                          {"topEye", vec(0.f, 65.f + elevation, 0.f)},
                                          {"target", vec(0.f, elevation, 0.f)},
                                          {"nodes", std::move(nodes)},
                                          {"roads", std::move(roads)},
                                          {"mesh", meshValue(*surface)}};
    std::filesystem::create_directories(destination / name);
    write(destination / name / "fixture.json", require(spec.toJson()));
    std::cout << name << ": surface vertices=" << surface->getVertexCount()
              << " triangles=" << surface->getIndexCount() / 3 << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: road_parity_fixture OUTPUT_DIRECTORY");
        for (const auto& name : std::array{"straight",
                                           "curve",
                                           "slope",
                                           "bridge",
                                           "tee",
                                           "cross",
                                           "width-profile",
                                           "banked",
                                           "s-curve",
                                           "crest",
                                           "valley",
                                           "curved-uphill",
                                           "variable-bank",
                                           "negative-bank",
                                           "full-roll-positive",
                                           "full-roll-negative",
                                           "bank-pivot-left",
                                           "bank-pivot-right",
                                           "bank-pivot-varying",
                                           "curve-width",
                                           "curve-width-bank",
                                           "strength-quarter",
                                           "strength-full",
                                           "control-xz",
                                           "control-xyz",
                                           "control-circular",
                                           "control-circular-mirror",
                                           "control-circular-zero-bump",
                                           "control-circular-mirror-zero-bump",
                                           "sampling-resolution-1",
                                           "sampling-resolution-10",
                                           "sampling-angle-1",
                                           "sampling-angle-20",
                                           "sampling-transverse-3",
                                           "sampling-created-default"})
            exportFixture(std::filesystem::path(argv[1]), name);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
