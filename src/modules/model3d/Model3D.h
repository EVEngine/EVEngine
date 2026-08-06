#pragma once

#include "common/Module.h"
#include "model3d/ModelData.h"

#include <string>

namespace eve {
class Data;

namespace model3d {

/**
 * Resource module for decoding 3D models via medialoader (Assimp).
 * Produces ModelData; GPU upload is graphics' responsibility.
 */
class Model3D : public Module {
public:
    Module_REG(Model3D);

    Model3D();
    ~Model3D() override;

    /**
     * Decode model from in-memory bytes.
     * @param data Encoded model bytes (e.g. FileData / ByteData).
     * @param hintExt Extension hint for Assimp (e.g. ".obj"). If empty and
     *        data is FileData, uses FileData::getExtension().
     */
    ModelData *newModelData(Data *data, std::string hintExt = "");

    /**
     * Decode model from a VFS path (physfs). Uses EveFileSystem so sidecar
     * files (.mtl, textures) resolve through the same filesystem.
     */
    ModelData *newModelDataFromFile(std::string path);
};

}  // namespace model3d
}  // namespace eve
