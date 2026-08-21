#include "common/Capability.h"
#include "common/ProcgenQuery.h"
#include "procgen/Procgen.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

class ProcgenQueryImpl final : public eve::IProcgenQuery {
public:
    Procgen *pg() { return eve::ModuleManager::getInstance<Procgen>("Procgen"); }

    std::vector<std::string> algorithms() override {
        std::vector<std::string> out;
        if (auto *p = pg())
            for (int i = 0; i < p->getAlgorithmCount(); ++i) out.push_back(p->getAlgorithmId(i));
        return out;
    }

    std::vector<std::string> meshRecipes() override {
        std::vector<std::string> out;
        if (auto *p = pg())
            for (int i = 0; i < p->getMeshRecipeCount(); ++i)
                out.push_back(p->getMeshRecipeId(i));
        return out;
    }

    std::vector<std::string> textureRecipes() override {
        std::vector<std::string> out;
        if (auto *p = pg())
            for (int i = 0; i < p->getTextureRecipeCount(); ++i)
                out.push_back(p->getTextureRecipeId(i));
        return out;
    }

    std::vector<std::string> pbrRecipes() override {
        std::vector<std::string> out;
        if (auto *p = pg())
            for (int i = 0; i < p->getPbrRecipeCount(); ++i) out.push_back(p->getPbrRecipeId(i));
        return out;
    }

    std::string generateMap(const std::string &algorithm, int width, int height, uint32_t seed,
                            const std::vector<std::pair<std::string, std::string>> &params,
                            std::string *err) override {
        auto *p = pg();
        if (!p) {
            if (err) *err = "Procgen module not available";
            return {};
        }
        auto *par = p->newParams();
        par->setSize(width, height);
        par->setSeed(seed);
        for (const auto &kv : params) {
            if (kv.second.find_first_not_of("0123456789-") == std::string::npos)
                par->setInt(kv.first, std::atoi(kv.second.c_str()));
            else
                par->setString(kv.first, kv.second);
        }
        auto *grid = p->generate(algorithm, par);
        if (!grid) {
            if (err) *err = "generate failed: " + p->lastError();
            return {};
        }
        std::string json = p->gridToJson(grid);
        delete grid;
        return json;
    }

    std::string buildMesh(const std::string &recipe, uint32_t seed, int width, int height,
                          int depth, std::string *err) override {
        auto *p = pg();
        if (!p) {
            if (err) *err = "Procgen module not available";
            return {};
        }
        auto *par = p->newParams();
        par->setSeed(seed);
        if (width > 0) par->setInt("width", width);
        if (height > 0) par->setInt("height", height);
        if (depth > 0) par->setInt("depth", depth);
        auto *mesh = p->buildMesh(recipe, par);
        if (!mesh) {
            if (err) *err = "build failed: " + p->lastError();
            return {};
        }
        std::string json = "{\"vertices\":" + std::to_string(mesh->getVertexCount()) +
                           ",\"triangles\":" + std::to_string(mesh->getIndexCount() / 3) + "}";
        delete mesh;
        return json;
    }
};

}  // namespace

void registerProcgenCapabilities() {
    static ProcgenQueryImpl impl;
    eve::cap::provide<eve::IProcgenQuery>(&impl);
}

}  // namespace eve::procgen
