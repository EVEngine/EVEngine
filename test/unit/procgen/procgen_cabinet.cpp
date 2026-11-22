#include "procgen/algorithms/Cabinet.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace eve::procgen;

TEST_CASE("procgen.cabinet.stylesAndGeometry") {
    for (const auto* style : {"european", "chinese", "minimal", "modern"}) {
        Params p;
        p.setString("style", style);
        auto result = generateCabinet(p);
        REQUIRE(result.ok());
        const auto& mesh = result.value();
        REQUIRE(!mesh.empty());
        REQUIRE_EQ(mesh.getMeta("style", ""), std::string(style));
        REQUIRE(std::find(mesh.groupNames().begin(), mesh.groupNames().end(), "door.1") != mesh.groupNames().end());
        REQUIRE(std::find(mesh.groupNames().begin(), mesh.groupNames().end(), "drawer.1") != mesh.groupNames().end());
        for (int i = 0; i < mesh.getVertexCount(); ++i) {
            REQUIRE(std::isfinite(mesh.getPositionX(i)));
            REQUIRE(mesh.getPositionY(i) >= -0.00001f);
            REQUIRE(mesh.getPositionY(i) <= 2.00001f);
        }
        for (int i = 0; i < mesh.getIndexCount(); i += 3) {
            const int a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
            REQUIRE((a >= 0 && a < mesh.getVertexCount()));
            REQUIRE((b >= 0 && b < mesh.getVertexCount()));
            REQUIRE((c >= 0 && c < mesh.getVertexCount()));
            const float ux  = mesh.getPositionX(b) - mesh.getPositionX(a),
                        uy  = mesh.getPositionY(b) - mesh.getPositionY(a),
                        uz  = mesh.getPositionZ(b) - mesh.getPositionZ(a);
            const float vx  = mesh.getPositionX(c) - mesh.getPositionX(a),
                        vy  = mesh.getPositionY(c) - mesh.getPositionY(a),
                        vz  = mesh.getPositionZ(c) - mesh.getPositionZ(a);
            const float dot = (uy * vz - uz * vy) * mesh.getNormalX(a) + (uz * vx - ux * vz) * mesh.getNormalY(a) +
                              (ux * vy - uy * vx) * mesh.getNormalZ(a);
            REQUIRE(dot > 0.f);
        }
        auto again = generateCabinet(p);
        REQUIRE(again.ok());
        REQUIRE(mesh.positions() == again.value().positions());
        REQUIRE(mesh.indices() == again.value().indices());
    }
}

TEST_CASE("procgen.cabinet.openAndCounts") {
    Params p;
    p.setBool("legs", false);
    p.setInt("doors", 0);
    p.setInt("drawers", 0);
    p.setInt("levels", 1);
    p.setInt("columns", 1);
    auto result = generateCabinet(p);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().getGroupCount(), 2);
    p.setInt("doors", 4);
    p.setInt("drawers", 3);
    p.setInt("columns", 3);
    p.setInt("levels", 4);
    auto populated = generateCabinet(p);
    REQUIRE(populated.ok());
    const auto& groups = populated.value().groupNames();
    REQUIRE(std::find(groups.begin(), groups.end(), "door.3") != groups.end());
    REQUIRE(std::find(groups.begin(), groups.end(), "drawer.2") != groups.end());
    REQUIRE(std::find(groups.begin(), groups.end(), "legs") == groups.end());
}

TEST_CASE("procgen.cabinet.rejectsInvalidLayouts") {
    for (const auto* key : {"cabinetWidth", "cabinetHeight", "depth", "thickness", "legHeight"}) {
        for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            Params p;
            p.setFloat(key, invalid);
            auto result = generateCabinet(p);
            REQUIRE(!result.ok());
        }
    }
    for (const auto* key : {"columns", "levels", "doors", "drawers"}) {
        Params p;
        p.setInt(key, 17);
        auto result = generateCabinet(p);
        REQUIRE(!result.ok());
    }
    Params p;
    p.setFloat("cabinetHeight", 0.2f);
    auto small = generateCabinet(p);
    REQUIRE(!small.ok());
    p = Params();
    p.setString("style", "unknown");
    auto unknown = generateCabinet(p);
    REQUIRE(!unknown.ok());
}

TEST_CASE("procgen.cabinet.materialPartitionsAndGrain") {
    for (const auto* style : {"european", "chinese", "minimal", "modern"}) {
        Params p;
        p.setString("style", style);
        auto result = generateCabinet(p);
        REQUIRE(result.ok());
        const auto& mesh    = result.value();
        int         handles = 0;
        const auto  bodyIt  = std::find(mesh.groupNames().begin(), mesh.groupNames().end(), "body");
        auto        body    = mesh.copyGroup(int(bodyIt - mesh.groupNames().begin()));
        REQUIRE(body != nullptr);
        float bodyTop = 0;
        for (int i = 0; i < body->getVertexCount(); ++i) bodyTop = std::max(bodyTop, body->getPositionY(i));
        const bool crown = std::string(style) == "european" || std::string(style) == "chinese";
        REQUIRE(std::abs(bodyTop - (crown ? 1.95f : 2.0f)) < 0.00001f);
        for (int g = 0; g < mesh.getGroupCount(); ++g) {
            const auto name = mesh.getGroupName(g);
            const auto role = mesh.getMeta("material." + name, "");
            REQUIRE(
                (role == "exterior" || role == "interior" || role == "trim" || role == "hardware" || role == "legs"));
            if (name.ends_with(".hardware")) {
                ++handles;
                REQUIRE_EQ(role, std::string("hardware"));
                if (name.starts_with("door.")) {
                    auto handle = mesh.copyGroup(g);
                    REQUIRE(handle != nullptr);
                    for (int i = 0; i < handle->getVertexCount(); ++i)
                        REQUIRE(std::abs(handle->getPositionX(i)) < 0.1f);
                }
            }
            if (name.ends_with(".interior")) REQUIRE_EQ(role, std::string("interior"));
            if (name == "door.0" || name == "drawer.0") {
                auto part = mesh.copyGroup(g);
                REQUIRE(part != nullptr);
                int frontVertices = 0;
                for (int i = 0; i < part->getVertexCount(); ++i) {
                    if (part->getNormalZ(i) < 0.99f) continue;
                    ++frontVertices;
                    if (name == "door.0") {
                        REQUIRE(std::abs(part->getUvU(i) - part->getPositionX(i)) < 0.00001f);
                        REQUIRE(std::abs(part->getUvV(i) - part->getPositionY(i)) < 0.00001f);
                    } else {
                        REQUIRE(std::abs(part->getUvU(i) - part->getPositionY(i)) < 0.00001f);
                        REQUIRE(std::abs(part->getUvV(i) - part->getPositionX(i)) < 0.00001f);
                    }
                }
                REQUIRE_EQ(frontVertices, 4);
            }
        }
        REQUIRE_EQ(handles, std::string(style) == "minimal" ? 0 : 4);
    }
}
