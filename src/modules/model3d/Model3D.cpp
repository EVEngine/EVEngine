#include "model3d/Model3D.h"
#include "model3d/EveFileSystem.h"

#include "common/Data.h"
#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

#include "medialoader/Exception.h"
#include "medialoader/model/ModelLoader.h"

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

medialoader::ModelScene loadOrThrow(medialoader::ModelLoader &loader, const void *data, size_t size,
                                    const char *hint) {
    try {
        return loader.loadFromMemory(data, size, hint);
    } catch (const medialoader::Exception &e) {
        throw eve::Exception("%s", e.what());
    }
}

}  // namespace

ModelData *Model3D::newModelData(Data *data, std::string hintExt) {
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

    auto scene = loadOrThrow(loader, data->getData(), data->getSize(), hint.c_str());
    if (scene.empty())
        throw eve::Exception("Could not decode model data");

    std::string uri;
    if (auto *fd = dynamic_cast<filesystem::FileData *>(data))
        uri = "file://" + fd->getFilename();

    return new ModelData(std::move(scene), std::move(uri));
}

ModelData *Model3D::newModelDataFromFile(std::string path) {
    if (path.empty())
        throw eve::Exception("Model3D::newModelDataFromFile: empty path");

    filesystem::Filesystem *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs)
        fs = filesystem::Filesystem::create();

    EveFileSystem eveFs(fs);
    medialoader::ModelLoader loader(&eveFs);

    try {
        auto scene = loader.loadFromPath(path.c_str());
        if (scene.empty())
            throw eve::Exception("Could not load model: %s", path.c_str());
        return new ModelData(std::move(scene), "file://" + path);
    } catch (const medialoader::Exception &e) {
        throw eve::Exception("%s", e.what());
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
    md.addFunc("hasNormals", &ModelData::hasNormals);
    md.addFunc("hasTexCoords", &ModelData::hasTexCoords);
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
    cls.addFunc("newModelData", &Model3D::newModelData);
    cls.addFunc("newModelDataFromFile", &Model3D::newModelDataFromFile);
}

}  // namespace model3d
}  // namespace eve
