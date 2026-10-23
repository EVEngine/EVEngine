#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "common/Value.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetworkCodec.h"

using eve::Value;
using namespace eve::procgen::road;
namespace {
template <class T>
T checked(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
Value read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return checked(Value::fromJson(std::string(std::istreambuf_iterator<char>(input), {})));
}
void write(const std::filesystem::path& path, const Value& value) {
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << checked(value.toJson());
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: road_bundled_fixture DIRECTORY");
        const std::filesystem::path root = argv[1];
        Value::Array                roads;
        for (const auto& entry : std::filesystem::directory_iterator(root / "networks")) {
            if (entry.path().extension() != ".json") continue;
            auto            network = checked(restoreRoadNetwork(read(entry.path())));
            RoadBakeOptions options;
            options.includeNavigation   = false;
            options.includeMarkings     = false;
            options.includeJunctions    = false;
            options.includePlacements   = false;
            options.includePiers        = false;
            options.pathSegmentsPerEdge = 256;
            auto baked                  = checked(bakeRoadNetwork(network, options));
            auto snapshot               = checked(captureRoadNetwork(network));
            auto restored               = checked(restoreRoadNetwork(snapshot));
            auto again                  = checked(bakeRoadNetwork(restored, options));
            if (again.mesh.positions() != baked.mesh.positions() || again.mesh.indices() != baked.mesh.indices() ||
                again.mesh.normals() != baked.mesh.normals() || again.mesh.uvs() != baked.mesh.uvs() ||
                again.mesh.groupNames() != baked.mesh.groupNames())
                throw std::runtime_error("Network persistence changed generated mesh");
            Value::Array parts;
            for (int group = 0; group < baked.mesh.getGroupCount(); ++group) {
                auto mesh = baked.mesh.copyGroup(group);
                if (!mesh || mesh->getIndexCount() == 0) continue;
                Value::Array positions, normals, uvs, indices;
                for (int i = 0; i < mesh->getVertexCount(); ++i) {
                    positions.emplace_back(mesh->getPositionX(i));
                    positions.emplace_back(mesh->getPositionY(i));
                    positions.emplace_back(mesh->getPositionZ(i));
                    normals.emplace_back(mesh->getNormalX(i));
                    normals.emplace_back(mesh->getNormalY(i));
                    normals.emplace_back(mesh->getNormalZ(i));
                    uvs.emplace_back(mesh->getUvU(i));
                    uvs.emplace_back(mesh->getUvV(i));
                }
                for (int i = 0; i < mesh->getIndexCount(); ++i) indices.emplace_back(mesh->getIndex(i));
                parts.emplace_back(Value::Object{{"group", baked.mesh.getGroupName(group)},
                                                 {"positions", std::move(positions)},
                                                 {"normals", std::move(normals)},
                                                 {"uvs", std::move(uvs)},
                                                 {"indices", std::move(indices)}});
            }
            roads.emplace_back(Value::Object{{"id", entry.path().stem().string()}, {"parts", std::move(parts)}});
            write(root / "networks" / (entry.path().stem().string() + ".canonical"), snapshot);
            std::cout << entry.path().stem().string() << " triangles=" << baked.mesh.getIndexCount() / 3 << '\n';
        }
        write(root / "native-roads.json",
              Value::Object{{"schema", "eve.tests.bundled-native-roads"}, {"version", 1}, {"roads", std::move(roads)}});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
