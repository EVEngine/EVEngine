#pragma once

#include "common/Module.h"
#include "model3d/ModelData.h"

#include <string>

namespace eve {
class Data;

namespace graphics {
class Graphics;
class Renderable3D;
}  // namespace graphics

namespace model3d {
class ModelData;

/**
 * @brief Toggles for the Assimp post-processing steps applied by medialoader on
 * decode. All default to on (matches the historical behavior); disabling a
 * step changes vertex/triangle layout (e.g. joinIdenticalVertices=false keeps
 * per-face vertices for hard-edged flat shading). Normals are generated with
 * aiProcess_GenSmoothNormals when a mesh lacks them (see the configure-time
 * medialoader patch in CMakeLists.txt), keeping hard seams beyond the 80-degree
 * smoothing angle.
 */
struct ModelLoadOptions {
    bool triangulate = true;
    /** Missing normals are generated smoothly (per-vertex) instead of flat. */
    bool generateNormalsIfMissing = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = true;
    bool improveCacheLocality = true;
};

/**
 * @brief Resource module for decoding 3D models via medialoader (Assimp).
 * Produces ModelData; GPU upload is graphics' responsibility.
 */
class Model3D : public Module {
public:
    Module_REG(Model3D);

    Model3D();
    ~Model3D() override;

    /**
     * @brief Decode model from in-memory bytes.
     * @param data Encoded model bytes (e.g. FileData / ByteData).
     * @param hintExt Extension hint for Assimp (e.g. ".obj"). If empty and
     *        data is FileData, uses FileData::getExtension().
     */
    ModelData *newModelData(Data *data, std::string hintExt = "");

    /** @brief newModelData with explicit decode options. */
    ModelData *newModelData(Data *data, std::string hintExt, const ModelLoadOptions &options);

    /**
     * @brief Decode model from a VFS path (physfs). Uses EveFileSystem so sidecar
     * files (.mtl, textures) resolve through the same filesystem.
     */
    ModelData *newModelDataFromFile(std::string path);

    /** @brief newModelDataFromFile with explicit decode options. */
    ModelData *newModelDataFromFile(std::string path, const ModelLoadOptions &options);

    /**
     * Assemble one Renderable3D for a mesh of a decoded model: node transform
     * baked into vertices, material base color / metallic / roughness applied,
     * and albedo / normal / height textures loaded (embedded or through the
     * VFS). Script-friendly counterpart of the C++ buildRenderable helpers.
     */
    graphics::Renderable3D *createRenderable(graphics::Graphics *gfx, ModelData *model,
                                             int meshIndex);
};

}  // namespace model3d
}  // namespace eve
