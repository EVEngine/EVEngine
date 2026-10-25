#include "OptionalFixtures.h"
#include <array>
#include <cmath>
#include <stdexcept>

using namespace eve::procgen::road;
namespace {
template <class T>
T checked(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
void box(RoadFacilityPrefab& mesh, float x0, float x1, float y0, float y1, float z0, float z1) {
    const float p[8][3]{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    const int   faces[6][4]{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    const float n[6][3]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            const auto& v = p[faces[f][i]];
            mesh.vertices.push_back(
                {v[0], v[1], v[2], n[f][0], n[f][1], n[f][2], i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f});
        }
        mesh.triangles.push_back({base, base + 1, base + 2, RoadMaterial::Curb});
        mesh.triangles.push_back({base, base + 2, base + 3, RoadMaterial::Curb});
    }
}
RoadContinuousFacility profile(std::uint32_t id, float offset, float width, float height) {
    RoadContinuousFacility f;
    f.id            = id;
    f.lateralOffset = offset;
    f.profile       = {{-width / 2, 0, RoadMaterial::Curb},
                       {width / 2, 0, RoadMaterial::Curb},
                       {width / 2, height, RoadMaterial::Curb},
                       {-width / 2, height, RoadMaterial::Curb}};
    return f;
}
RoadContinuousFacility prop(std::uint32_t id, float offset, bool lamp) {
    auto  f            = profile(id, offset, .3f, .15f);
    auto& p            = f.prefab;
    p.assetId          = lamp ? "fixture.lamp.module8" : "fixture.planter-bench.module8";
    p.length           = 8;
    p.middleCopies     = 8;
    p.automaticSpacing = true;
    // A continuous plinth supplies an explicit eight-metre source bound.
    box(p, -.12f, .12f, 0, .12f, 0, 8);
    if (lamp) {
        box(p, -.09f, .09f, 0, 5, 3.9f, 4.1f);
        const float sign = offset > 0 ? -1.f : 1.f;
        box(p, sign < 0 ? -1.3f : 0, sign < 0 ? 0 : 1.3f, 4.8f, 5, 3.9f, 4.1f);
        box(p, sign < 0 ? -1.4f : .9f, sign < 0 ? -.9f : 1.4f, 4.65f, 4.85f, 3.7f, 4.3f);
    } else {
        box(p, -.55f, .55f, 0, .6f, 1, 2.4f);
        box(p, -.12f, .12f, .6f, 2.5f, 1.6f, 1.8f);
        box(p, -.75f, .75f, 1.8f, 3, 1.1f, 2.5f);
        box(p, -.55f, .55f, .45f, .6f, 4.5f, 6.5f);
        box(p, -.55f, -.4f, 0, .45f, 4.7f, 6.3f);
        box(p, .4f, .55f, 0, .45f, 4.7f, 6.3f);
        box(p, .4f, .55f, .6f, 1.15f, 4.5f, 6.5f);
    }
    return f;
}
RoadStyle base() {
    RoadStyle s;
    s.laneWidth           = 3;
    s.automaticStructures = false;
    s.crossSection        = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    return s;
}
void road(RoadNetwork& network, RoadStyle style, float angle, float grade, float height, float z = 0, float roll = 0) {
    const float radians = angle * 3.14159265358979323846f / 180;
    const float dx = 32 * std::cos(radians), dz = 32 * std::sin(radians);
    auto        a = checked(network.addNode(-dx, height - 32 * grade, z - dz));
    auto        b = checked(network.addNode(dx, height + 32 * grade, z + dz));
    (void)checked(network.addEdge(
        a, b, {{-dx, height - 32 * grade, z - dz, 1, roll}, {dx, height + 32 * grade, z + dz, 1, roll}}, 1, 1, style));
}
}  // namespace
RoadNetwork optionalFixture(const std::string& name) {
    RoadNetwork network;
    auto        s = base();
    if (name.starts_with("interchange-")) {
        float angle = 30, grade = .04f, roll = 0;
        if (name == "interchange-grade8-angle60") {
            angle = 60;
            grade = .08f;
        }
        if (name == "interchange-grade12-angle90") {
            angle = 90;
            grade = .12f;
        }
        if (name == "interchange-banked-angle45") {
            angle = 45;
            grade = .08f;
            roll  = 5;
        }
        road(network, s, 0, 0, 0);
        s.crossSectionClosed = true;
        s.crossSection       = {{-3, 0, RoadMaterial::Asphalt},
                                {3, 0, RoadMaterial::Deck},
                                {3, -.8f, RoadMaterial::Deck},
                                {-3, -.8f, RoadMaterial::Deck}};
        s.facilities         = {profile(21, -3.6f, .22f, 1.1f), profile(22, 3.6f, .22f, 1.1f)};
        auto  support        = profile(23, 0, .1f, .1f);
        auto& p              = support.prefab;
        p.assetId            = "fixture.bridge.supports";
        p.length             = 64 * std::sqrt(1 + grade * grade);
        p.middleCopies       = 1;
        box(p, -.35f, .35f, -1.1f, -.8f, 0, p.length);
        for (float station : {-24.f, -8.f, 8.f, 24.f}) {
            const float position = (station + 32) / 64 * p.length;
            box(p, -.55f, .55f, -(10 + station * grade), -.8f, position - .55f, position + .55f);
        }
        s.facilities.push_back(support);
        road(network, s, angle, grade, 10, 0, roll);
        return network;
    }
    const bool median = name == "option-median" || name == "option-boulevard";
    const bool lamps  = name == "option-streetlights" || name == "option-boulevard";
    const bool decor  = name == "option-roadside-decor" || name == "option-boulevard";
    if (!median && !lamps && !decor) throw std::runtime_error("Unknown optional fixture: " + name);
    if (median) s.facilities.push_back(profile(31, -4, 1.2f, .8f));
    if (lamps) s.facilities.push_back(prop(32, 4.5f, true));
    if (decor) s.facilities.push_back(prop(33, lamps ? 6.5f : 4.5f, false));
    if (!median) {
        if (lamps) s.facilities.push_back(prop(34, -4.5f, true));
        if (decor) s.facilities.push_back(prop(35, lamps ? -6.5f : -4.5f, false));
    }
    road(network, s, 0, 0, 0, median ? -4.f : 0.f);
    if (median) {
        auto other = base();
        if (lamps) other.facilities.push_back(prop(36, -4.5f, true));
        if (decor) other.facilities.push_back(prop(37, -6.5f, false));
        road(network, other, 0, 0, 0, 4);
    }
    return network;
}
