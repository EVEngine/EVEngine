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
        if (auto* p = pg())
            for (int i = 0; i < p->getAlgorithmCount(); ++i) out.push_back(p->getAlgorithmId(i));
        return out;
    }

    std::vector<std::string> meshRecipes() override {
        std::vector<std::string> out;
        if (auto* p = pg())
            for (int i = 0; i < p->getMeshRecipeCount(); ++i) out.push_back(p->getMeshRecipeId(i));
        return out;
    }

    std::vector<std::string> textureRecipes() override {
        std::vector<std::string> out;
        if (auto* p = pg())
            for (int i = 0; i < p->getTextureRecipeCount(); ++i)
                out.push_back(p->getTextureRecipeId(i));
        return out;
    }

    std::vector<std::string> pbrRecipes() override {
        std::vector<std::string> out;
        if (auto* p = pg())
            for (int i = 0; i < p->getPbrRecipeCount(); ++i) out.push_back(p->getPbrRecipeId(i));
        return out;
    }

    eve::Result<std::string> generateMap(const std::string& algorithm, int width, int height, uint32_t seed,
                                         const std::vector<std::pair<std::string, std::string>>& params) override {
        auto* p = pg();
        if (!p) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "Procgen module not available", "procgen"));
        }
        auto parResult = Procgen::newParamsHandle();
        if (!parResult) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "parameter allocation failed", "procgen"));
        }
        const auto parRef  = std::move(parResult).takeValue();
        auto       parView = Procgen::resolve(parRef);
        if (!parView.isBound()) {
            Procgen::release(parRef).ignore("release failed after stale parameter lookup");
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "parameter handle became stale", "procgen"));
        }
        auto* par = parView.get();
        par->setSize(width, height);
        par->setSeed(seed);
        for (const auto& kv : params) {
            if (kv.second.find_first_not_of("0123456789-") == std::string::npos)
                par->setInt(kv.first, std::atoi(kv.second.c_str()));
            else
                par->setString(kv.first, kv.second);
        }
        auto gridResult = p->generateHandle(algorithm, parRef);
        if (!gridResult) {
            Procgen::release(parRef).ignore("release failed after map generation failure");
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "generate failed: " + gridResult.status().describe(), "procgen"));
        }
        auto jsonResult = p->gridToJson(std::move(gridResult).takeValue());
        if (!jsonResult) {
            Procgen::release(parRef).ignore("release failed after map serialization failure");
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "serialization failed: " + jsonResult.status().describe(), "procgen"));
        }
        std::string json = std::move(jsonResult).takeValue();
        Procgen::release(parRef).ignore("release failed after map generation");
        return eve::Result<std::string>::success(std::move(json));
    }

    eve::Result<std::string> buildMesh(const std::string& recipe, uint32_t seed, int width, int height,
                                       int depth) override {
        auto* p = pg();
        if (!p) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "Procgen module not available", "procgen"));
        }
        auto parResult = Procgen::newParamsHandle();
        if (!parResult) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "parameter allocation failed", "procgen"));
        }
        const auto parRef  = std::move(parResult).takeValue();
        auto       parView = Procgen::resolve(parRef);
        if (!parView.isBound()) {
            Procgen::release(parRef).ignore("release failed after stale parameter lookup");
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "parameter handle became stale", "procgen"));
        }
        auto* par = parView.get();
        par->setSeed(seed);
        if (width > 0) par->setInt("width", width);
        if (height > 0) par->setInt("height", height);
        if (depth > 0) par->setInt("depth", depth);
        auto meshResult = p->buildMeshHandle(recipe, parRef);
        if (!meshResult) {
            Procgen::release(parRef).ignore("release failed after mesh generation failure");
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "build failed: " + meshResult.status().describe(), "procgen"));
        }
        const auto meshRef = std::move(meshResult).takeValue();
        auto       mesh    = p->resolveMeshBuild(meshRef);
        if (!mesh.isBound()) {
            p->releaseMeshBuild(meshRef).ignore("release failed after stale mesh lookup");
            Procgen::release(parRef).ignore("release failed after stale mesh lookup");
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "build failed: mesh handle became stale", "procgen"));
        }
        std::string json = "{\"vertices\":" + std::to_string(mesh->getVertexCount()) +
                           ",\"triangles\":" + std::to_string(mesh->getIndexCount() / 3) + "}";
        p->releaseMeshBuild(meshRef).ignore("release mesh after capability query");
        Procgen::release(parRef).ignore("release failed after mesh generation");
        return eve::Result<std::string>::success(std::move(json));
    }
};

}  // namespace

void registerProcgenCapabilities() {
    static ProcgenQueryImpl impl;
    eve::cap::provide<eve::IProcgenQuery>(&impl);
}

}  // namespace eve::procgen
