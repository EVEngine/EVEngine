#include "model3d/Model3D.h"
#include "model3d/EveFileSystem.h"
#include "model3d/ModelRenderer.h"

#include "common/Data.h"
#include "common/Exception.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "image/ImageData.h"

#include "medialoader/Exception.h"
#include "medialoader/model/ModelLoader.h"

#include "model3d/ModelLoader.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cctype>
#include <functional>

namespace eve {
namespace model3d {

Module_IMPL(Model3D, new Model3D());

Model3D::Model3D() = default;
Model3D::~Model3D() = default;

namespace {

std::string lowerExt(std::string ext) {
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string ensureDotExt(std::string ext) {
    ext = lowerExt(std::move(ext));
    if (ext.empty())
        return {};
    if (ext[0] != '.')
        ext.insert(ext.begin(), '.');
    return ext;
}

medialoader::LoadOptions toMedialoader(const ModelLoadOptions &opt) {
    medialoader::LoadOptions m;
    m.triangulate = opt.triangulate;
    m.generateNormalsIfMissing = opt.generateNormalsIfMissing;
    m.joinIdenticalVertices = opt.joinIdenticalVertices;
    m.flipUVs = opt.flipUVs;
    m.improveCacheLocality = opt.improveCacheLocality;
    return m;
}

medialoader::ModelScene loadOrThrow(medialoader::ModelLoader &loader, const void *data, size_t size,
                                    const char *hint, const medialoader::LoadOptions &opt) {
    try {
        return loader.loadFromMemory(data, size, hint, opt);
    } catch (const medialoader::Exception &e) {
        throw eve::Exception("%s", e.what());
    }
}

}  // namespace

ModelData *Model3D::newModelData(Data *data, std::string hintExt) {
    return newModelData(data, std::move(hintExt), ModelLoadOptions{});
}

ModelData *Model3D::newModelData(Data *data, std::string hintExt,
                                 const ModelLoadOptions &options) {
    if (data == nullptr || data->getData() == nullptr || data->getSize() == 0)
        throw eve::Exception("Cannot decode empty model data");

    std::string hint = ensureDotExt(std::move(hintExt));
    if (hint.empty()) {
        if (auto *fd = dynamic_cast<filesystem::FileData *>(data))
            hint = ensureDotExt(fd->getExtension());
    }

    // Prefer VFS for sidecar resolution when FileData carries a filename.
    filesystem::Filesystem *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs)
        fs = filesystem::Filesystem::create();

    EveFileSystem eveFs(fs);
    medialoader::ModelLoader loader(&eveFs);

    auto scene = loadOrThrow(loader, data->getData(), data->getSize(), hint.c_str(),
                             toMedialoader(options));
    if (scene.empty())
        throw eve::Exception("Could not decode model data");

    std::string uri;
    if (auto *fd = dynamic_cast<filesystem::FileData *>(data))
        uri = "file://" + fd->getFilename();

    return new ModelData(std::move(scene), std::move(uri));
}

ModelData *Model3D::newModelDataFromFile(std::string path) {
    return newModelDataFromFile(std::move(path), ModelLoadOptions{});
}

ModelData *Model3D::newModelDataFromFile(std::string path, const ModelLoadOptions &options) {
    if (path.empty())
        throw eve::Exception("Model3D::newModelDataFromFile: empty path");

    // Route through the unified resource cache: options are part of the key,
    // so identical (path, options) requests share one decoded Assimp scene.
    const std::string key = modelCacheKey(path, options);
    eve::Resource *resource = eve::ResourceManager::getInstance().get(key);
    if (!resource)
        throw eve::Exception("Could not load model: %s", path.c_str());
    return static_cast<ModelData *>(resource);
}

graphics::Renderable3D *Model3D::createRenderable(graphics::Graphics *gfx, ModelData *model,
                                                  int meshIndex) {
    if (!gfx)
        throw eve::Exception("Model3D::createRenderable: null Graphics");
    if (!model)
        throw eve::Exception("Model3D::createRenderable: null ModelData");
    return buildRenderable(*gfx, model, meshIndex);
}

void Model3D::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Model3D::create, false);
    expose(cls);

    auto md = table.addClass<ModelData>(
        "ModelData", std::function<ModelData *()>([]() -> ModelData * { return nullptr; }), true);
    md.addFunc("empty", &ModelData::empty);
    md.addFunc("getMeshCount", &ModelData::getMeshCount);
    md.addFunc("getMaterialCount", &ModelData::getMaterialCount);
    md.addFunc("getVertexCount", &ModelData::getVertexCount);
    md.addFunc("getFaceCount", &ModelData::getFaceCount);
    md.addFunc("hasNormals", &ModelData::hasNormals);
    md.addFunc("hasTexCoords", &ModelData::hasTexCoords);
    md.addFunc("getMaterialIndex", &ModelData::getMaterialIndex);
    md.addFunc("getMaterialName", &ModelData::getMaterialName);
    md.addFunc("getMaterialBaseColorR", &ModelData::getMaterialBaseColorR);
    md.addFunc("getMaterialBaseColorG", &ModelData::getMaterialBaseColorG);
    md.addFunc("getMaterialBaseColorB", &ModelData::getMaterialBaseColorB);
    md.addFunc("getMaterialBaseColorA", &ModelData::getMaterialBaseColorA);
    md.addFunc("getMaterialMetallicFactor", &ModelData::getMaterialMetallicFactor);
    md.addFunc("getMaterialRoughnessFactor", &ModelData::getMaterialRoughnessFactor);
    md.addFunc("getMaterialOpacity", &ModelData::getMaterialOpacity);
    md.addFunc("getMaterialTwoSided", &ModelData::getMaterialTwoSided);
    md.addFunc("getMaterialTextureSlotCount", &ModelData::getMaterialTextureSlotCount);
    md.addFunc("getMaterialTexturePath", &ModelData::getMaterialTexturePath);
    md.addFunc("getMaterialTextureEmbeddedIndex", &ModelData::getMaterialTextureEmbeddedIndex);
    md.addFunc("getEmbeddedTextureCount", &ModelData::getEmbeddedTextureCount);
    md.addFunc("getEmbeddedTextureName", &ModelData::getEmbeddedTextureName);
    md.addFunc("getEmbeddedTextureWidth", &ModelData::getEmbeddedTextureWidth);
    md.addFunc("getEmbeddedTextureHeight", &ModelData::getEmbeddedTextureHeight);
    md.addFunc("getEmbeddedTextureImageData", &ModelData::getEmbeddedTextureImageData);
    md.addFunc("getMorphTargetCount", &ModelData::getMorphTargetCount);
    md.addFunc("getMorphTargetName", &ModelData::getMorphTargetName);
    md.addFunc("hasBones", &ModelData::hasBones);
    md.addFunc("getBoneCount", &ModelData::getBoneCount);
    md.addFunc("getBoneName", &ModelData::getBoneName);
    md.addFunc("getInverseBindMatrixElement", &ModelData::getInverseBindMatrixElement);
    md.addFunc("getBoneWeightCount", &ModelData::getBoneWeightCount);
    md.addFunc("getBoneWeightVertex", &ModelData::getBoneWeightVertex);
    md.addFunc("getBoneWeightValue", &ModelData::getBoneWeightValue);
    md.addFunc("getAnimationCount", &ModelData::getAnimationCount);
    md.addFunc("getAnimationName", &ModelData::getAnimationName);
}

void Model3D::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Model3D::getName);
    cls.addFunc("newModelData", static_cast<ModelData *(Model3D::*)(Data *, std::string)>(
                                    &Model3D::newModelData));
    cls.addFunc("newModelDataFromFile",
                static_cast<ModelData *(Model3D::*)(std::string)>(&Model3D::newModelDataFromFile));
    cls.addFunc("createRenderable", &Model3D::createRenderable);
}

}  // namespace model3d
}  // namespace eve
