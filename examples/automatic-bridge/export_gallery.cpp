// CPU-only consumer of the same preset, plan and bake contract used by EveScript.
#include "common/Value.h"
#include "procgen/road/BridgeGenerator.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

using namespace eve::procgen::road;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path destination(argv[1]);
    std::filesystem::create_directories(destination);
    std::ofstream                                 material(destination / "bridge.mtl");
    std::set<std::string>                         emittedMaterials;
    constexpr std::array<std::array<float, 3>, 8> colors = {{{.62f, .61f, .59f},
                                                             {.67f, .65f, .60f},
                                                             {.40f, .47f, .50f},
                                                             {.65f, .68f, .70f},
                                                             {.44f, .31f, .19f},
                                                             {.18f, .19f, .20f},
                                                             {.9f, .89f, .83f},
                                                             {.96f, .72f, .13f}}};
    eve::Value::Array                             records;
    for (int k = 0; k < 10; ++k) {
        const auto kind   = static_cast<BridgeKind>(k);
        const auto name   = std::string(bridgeKindName(kind));
        auto       preset = bridgePreset(kind);
        if (!preset.ok()) {
            std::cerr << preset.status().describe();
            return 1;
        }
        auto plan = planBridge(preset.value());
        if (!plan.ok()) {
            std::cerr << plan.status().describe();
            return 1;
        }
        auto result = bakeBridge(plan.value());
        if (!result.ok()) {
            std::cerr << result.status().describe();
            return 1;
        }
        const auto&   mesh = result.value();
        std::ofstream output(destination / (name + ".obj"));
        output << std::setprecision(9) << "mtllib bridge.mtl\n";
        for (int i = 0; i < mesh.getVertexCount(); ++i)
            output << "v " << mesh.getPositionX(i) << ' ' << mesh.getPositionY(i) << ' ' << mesh.getPositionZ(i)
                   << '\n';
        for (int i = 0; i < mesh.getVertexCount(); ++i)
            output << "vt " << mesh.getUvU(i) << ' ' << mesh.getUvV(i) << '\n';
        for (int i = 0; i < mesh.getVertexCount(); ++i)
            output << "vn " << mesh.getNormalX(i) << ' ' << mesh.getNormalY(i) << ' ' << mesh.getNormalZ(i) << '\n';
        for (int g = 0; g < mesh.getGroupCount(); ++g) {
            const auto group = mesh.getGroupName(g);
            if (!emittedMaterials.insert(group).second) continue;
            const auto at = group.find('@');
            if (at == std::string::npos) return 1;
            int surface = -1;
            for (int i = 0; i < 8; ++i)
                if (bridgeMaterialName(static_cast<BridgeMaterial>(i)) == group.substr(at + 1)) surface = i;
            if (surface < 0) return 1;
            const auto& rgb = colors[static_cast<std::size_t>(surface)];
            material << "newmtl " << group << "\nKd " << rgb[0] << ' ' << rgb[1] << ' ' << rgb[2]
                     << "\nKa 0.15 0.15 0.15\nKs 0.2 0.2 0.2\nNs 30\n";
        }
        int activeGroup = -1;
        for (int i = 0; i < mesh.getIndexCount(); i += 3) {
            const int group = mesh.getTriangleGroup(i / 3);
            if (group != activeGroup) {
                activeGroup = group;
                output << "g " << mesh.getGroupName(group) << "\nusemtl " << mesh.getGroupName(group) << '\n';
            }
            output << "f";
            for (int corner = 0; corner < 3; ++corner) {
                const int vertex = mesh.getIndex(i + corner) + 1;
                output << ' ' << vertex << '/' << vertex << '/' << vertex;
            }
            output << '\n';
        }
        if (!output.good() || !material.good()) return 1;
        eve::procgen::Params params;
        params.setString("bridgeType", name);
        auto defaults = applyBridgeRecipeDefaults(params);
        if (!defaults.ok()) {
            std::cerr << defaults.status().describe();
            return 1;
        }
        eve::Value::Object record;
        record["bridgeType"]          = eve::Value(name);
        record["parametersCanonical"] = eve::Value(params.canonicalString());
        record["members"]             = eve::Value(static_cast<std::int64_t>(plan.value().members.size()));
        record["vertices"]            = eve::Value(static_cast<std::int64_t>(mesh.getVertexCount()));
        record["triangles"]           = eve::Value(static_cast<std::int64_t>(mesh.getIndexCount() / 3));
        records.emplace_back(std::move(record));
        std::cout << name << " members=" << plan.value().members.size() << " vertices=" << mesh.getVertexCount()
                  << " triangles=" << mesh.getIndexCount() / 3 << '\n';
    }
    eve::Value::Object report;
    report["schema"]  = eve::Value("eve.bridge-preset-export");
    report["version"] = eve::Value(std::int64_t{1});
    report["policy"]  = eve::Value("read-only export report; unknown fields ignored; no runtime restore");
    report["bridges"] = eve::Value(std::move(records));
    auto json         = eve::Value(std::move(report)).toJson();
    if (!json.ok()) {
        std::cerr << json.status().describe();
        return 1;
    }
    std::ofstream metadata(destination / "presets.json");
    metadata << json.value() << '\n';
    return metadata.good() ? 0 : 1;
}
