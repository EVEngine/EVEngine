// Loads ModelData through the unified resource cache (common/Resource.h).
//
// The cache key serializes ModelLoadOptions, so the same file decoded with
// different options is distinct cached resources while identical requests
// share one decoded Assimp scene.

#include "model3d/ModelLoader.h"

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/Resource.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "medialoader/Exception.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/EveFileSystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace eve::model3d {
namespace {

std::string extensionOf(const std::string &path) {
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    std::string ext = path.substr(pos);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isModelPath(const std::string &path) {
    const std::string ext = extensionOf(path);
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" ||
           ext == ".3ds" || ext == ".blend" || ext == ".stl" || ext == ".ply" || ext == ".x" ||
           ext == ".lwo" || ext == ".lws" || ext == ".md5mesh" || ext == ".md5anim" ||
           ext == ".b3d" || ext == ".csm" || ext == ".irr" || ext == ".irrmesh" || ext == ".md2" ||
           ext == ".md3" || ext == ".ms3d" || ext == ".smd" || ext == ".vta" || ext == ".bvh" ||
           ext == ".ac" || ext == ".off" || ext == ".raw" || ext == ".ter" || ext == ".nff" ||
           ext == ".ndo" || ext == ".evmodel";
}

// Mirror of the option mapping in Model3D.cpp.
medialoader::LoadOptions toMedialoader(const ModelLoadOptions &opt) {
    medialoader::LoadOptions m;
    m.triangulate = opt.triangulate;
    m.generateNormalsIfMissing = opt.generateNormalsIfMissing;
    m.joinIdenticalVertices = opt.joinIdenticalVertices;
    m.flipUVs = opt.flipUVs;
    m.improveCacheLocality = opt.improveCacheLocality;
    return m;
}

int queryInt(const std::string &key, const std::string &name, int fallback) {
    const auto q = key.find('?');
    if (q == std::string::npos) return fallback;

    std::string rest = key.substr(q + 1);
    size_t pos = 0;
    while (pos < rest.size()) {
        const size_t amp = rest.find('&', pos);
        const std::string pair =
            rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const auto eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == name)
            return std::atoi(pair.c_str() + eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return fallback;
}

bool queryBool(const std::string &key, const std::string &name, bool fallback) {
    return queryInt(key, name, fallback ? 1 : 0) != 0;
}

ModelLoadOptions parseOptions(const std::string &key) {
    ModelLoadOptions o;
    o.triangulate = queryBool(key, "triangulate", o.triangulate);
    o.generateNormalsIfMissing = queryBool(key, "normals", o.generateNormalsIfMissing);
    o.joinIdenticalVertices = queryBool(key, "join", o.joinIdenticalVertices);
    o.flipUVs = queryBool(key, "flipuv", o.flipUVs);
    o.improveCacheLocality = queryBool(key, "cache", o.improveCacheLocality);
    return o;
}

class ModelLoader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "model"; }

    bool handlesPath(const std::string &normPath) const override {
        return isModelPath(eve::ResourceManager::pathOfKey(normPath));
    }

    eve::Resource *load(const std::string &key) override {
        const std::string path = eve::ResourceManager::pathOfKey(key);
        if (!isModelPath(path)) return nullptr;
        const ModelLoadOptions options = parseOptions(key);

        filesystem::Filesystem *fs =
            ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
        if (!fs) fs = filesystem::Filesystem::create();
        if (!fs) return nullptr;

        if (extensionOf(path) == ".evmodel") {
            filesystem::FileData *packedRaw = fs->read(path);
            if (!packedRaw) return nullptr;
            eve::ref<filesystem::FileData> packed(packedRaw);
            Model3D *module = ModuleManager::getInstance<Model3D>("Model3D");
            if (!module) module = Model3D::create();
            return module->newModelData(packed.get(), ".evmodel", options);
        }

        EveFileSystem eveFs(fs);
        medialoader::ModelLoader loader(&eveFs);
        try {
            auto scene = loader.loadFromPath(path.c_str(), toMedialoader(options));
            if (scene.empty())
                throw eve::Exception("Could not load model: %s", path.c_str());
            return new ModelData(std::move(scene), "file://" + path);
        } catch (const medialoader::Exception &e) {
            throw eve::Exception("%s", e.what());
        }
    }
};

struct Register {
    Register() {
        static ModelLoader loader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&loader, eve::caps::IAssetReloader::kCache);
    }
} g_register;

}  // namespace

std::string modelCacheKey(const std::string &path, const ModelLoadOptions &options) {
    std::string query;
    query += "triangulate=" + std::to_string(options.triangulate ? 1 : 0);
    query += "&normals=" + std::to_string(options.generateNormalsIfMissing ? 1 : 0);
    query += "&join=" + std::to_string(options.joinIdenticalVertices ? 1 : 0);
    query += "&flipuv=" + std::to_string(options.flipUVs ? 1 : 0);
    query += "&cache=" + std::to_string(options.improveCacheLocality ? 1 : 0);
    return eve::ResourceManager::makeKey(path, query);
}

ModelLoadOptions modelOptionsFromKey(const std::string &key) { return parseOptions(key); }

}  // namespace eve::model3d
