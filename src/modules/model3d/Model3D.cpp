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
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

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

constexpr char kEvModelMagic[4] = {'E', 'V', 'M', '1'};

bool unpackEvModel(const void *data, size_t size, const uint8_t **payload, size_t *payloadSize,
                   std::string *hint) {
    if (!data || size < 6 || std::memcmp(data, kEvModelMagic, 4) != 0) return false;
    const auto *bytes = static_cast<const uint8_t *>(data);
    const uint16_t n = uint16_t(bytes[4]) | (uint16_t(bytes[5]) << 8u);
    if (n == 0 || size < size_t(6u + n)) return false;
    hint->assign(reinterpret_cast<const char *>(bytes + 6), n);
    *payload = bytes + 6 + n;
    *payloadSize = size - 6 - n;
    return *payloadSize > 0;
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

    const void *encoded = data->getData();
    size_t encodedSize = data->getSize();
    if (hint == ".evmodel") {
        const uint8_t *payload = nullptr;
        std::string packedHint;
        if (!unpackEvModel(encoded, encodedSize, &payload, &encodedSize, &packedHint))
            throw eve::Exception("Invalid or truncated .evmodel envelope");
        encoded = payload;
        hint = ensureDotExt(std::move(packedHint));
    }

    // Prefer VFS for sidecar resolution when FileData carries a filename.
    filesystem::Filesystem *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs)
        fs = filesystem::Filesystem::create();

    EveFileSystem eveFs(fs);
    medialoader::ModelLoader loader(&eveFs);

    auto scene = loadOrThrow(loader, encoded, encodedSize, hint.c_str(),
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

bool Model3D::bakeModel(const std::string &sourcePath, const std::string &destinationPath) {
    if (sourcePath.empty() || destinationPath.empty()) return false;
    filesystem::Filesystem *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs) fs = filesystem::Filesystem::create();
    filesystem::FileData *sourceRaw = fs->read(sourcePath);
    if (!sourceRaw) return false;
    eve::ref<filesystem::FileData> source(sourceRaw);
    if (!source->getData() || source->getSize() == 0) return false;
    const std::string hint = ensureDotExt(source->getExtension());
    if (hint.empty() || hint == ".evmodel" || hint.size() > 65535u) return false;
    std::vector<uint8_t> packed;
    packed.reserve(6u + hint.size() + source->getSize());
    packed.insert(packed.end(), kEvModelMagic, kEvModelMagic + 4);
    const uint16_t n = static_cast<uint16_t>(hint.size());
    packed.push_back(static_cast<uint8_t>(n & 0xffu));
    packed.push_back(static_cast<uint8_t>((n >> 8u) & 0xffu));
    packed.insert(packed.end(), hint.begin(), hint.end());
    const auto *bytes = static_cast<const uint8_t *>(source->getData());
    packed.insert(packed.end(), bytes, bytes + source->getSize());
    try {
        fs->write(destinationPath, packed.data(), static_cast<int64_t>(packed.size()));
        return true;
    } catch (...) {
        return false;
    }
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
    md.addFunc("getVertexPosition", &ModelData::getVertexPosition);
    md.addFunc("getFaceVertexIndex", &ModelData::getFaceVertexIndex);
    md.addFunc("hasNormals", &ModelData::hasNormals);
    md.addFunc("hasTexCoords", &ModelData::hasTexCoords);
    md.addFunc("getTexCoordChannelCount", &ModelData::getTexCoordChannelCount);
    md.addFunc("hasTexCoordChannel", &ModelData::hasTexCoordChannel);
    md.addFunc("getTexCoord", &ModelData::getTexCoord);
    auto surfaceUv = table.addClass<SurfaceUv>(
        "SurfaceUv", std::function<SurfaceUv *()>([]() { return new SurfaceUv(); }), true);
    surfaceUv.addFunc("getU", [](SurfaceUv *value) { return value->u; });
    surfaceUv.addFunc("getV", [](SurfaceUv *value) { return value->v; });
    surfaceUv.addFunc("getBarycentricA", [](SurfaceUv *value) { return value->barycentricA; });
    surfaceUv.addFunc("getBarycentricB", [](SurfaceUv *value) { return value->barycentricB; });
    surfaceUv.addFunc("getBarycentricC", [](SurfaceUv *value) { return value->barycentricC; });
    surfaceUv.addFunc("getTriangleIndex", [](SurfaceUv *value) { return value->triangleIndex; });
    surfaceUv.addFunc("getUvChannel", [](SurfaceUv *value) { return value->uvChannel; });
    md.addFunc("mapSurfacePointToUv",
               [](ModelData *self, int meshIndex, int triangleIndex, float x, float y,
                  float z, int channel) -> SurfaceUv * {
                   auto mapped = self->mapSurfacePointToUv(meshIndex, triangleIndex, x, y, z,
                                                           channel);
                   if (!mapped.ok()) throw eve::Exception("%s", mapped.status().describe().c_str());
                   return new SurfaceUv(std::move(mapped).takeValue());
               });
    md.addFunc("hasTangents", &ModelData::hasTangents);
    md.addFunc("getTangent", &ModelData::getTangent);
    md.addFunc("getBitangent", &ModelData::getBitangent);
    md.addFunc("getVertexColorChannelCount", &ModelData::getVertexColorChannelCount);
    md.addFunc("hasVertexColorChannel", &ModelData::hasVertexColorChannel);
    md.addFunc("getVertexColor", &ModelData::getVertexColor);
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
    md.addFunc("getMaterialAlphaMode", &ModelData::getMaterialAlphaMode);
    md.addFunc("getMaterialAlphaCutoff", &ModelData::getMaterialAlphaCutoff);
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
    cls.addFunc("bakeModel", &Model3D::bakeModel);
}

}  // namespace model3d
}  // namespace eve
