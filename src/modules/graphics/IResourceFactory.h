#pragma once

// Narrow resource-factory interface of the Graphics backend.
// Consumers that only create textures / meshes / shaders / canvases (model3d,
// animation, avatar, sceneloader) depend on this instead of the full
// graphics::Graphics god class.

#include <cstdint>
#include <string>
#include <vector>

#include <assimp/matrix4x4.h>

struct aiMesh;

namespace eve::font {
class FontData;
}

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Canvas;
class Font;
class Mesh;
class Shader;
class Texture;
struct TextureCreateInfo;
struct TextureSampler;

/** @brief Texture / mesh / shader / canvas creation and release. */
class IResourceFactory {
public:
    virtual ~IResourceFactory() = default;

    virtual Texture *newTexture(int width, int height, const uint8_t *rgba, bool repeatU = false,
                                bool repeatV = false) = 0;
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba,
                                const TextureCreateInfo &info) = 0;
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces) = 0;
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces,
                                const TextureCreateInfo &info) = 0;
    virtual Texture *newTexture(image::ImageData *data) = 0;
    virtual Texture *newTexture(image::ImageData *data, const TextureCreateInfo &info) = 0;
    virtual Texture *newTextureFromImageData(image::ImageData *data, bool repeatU = false,
                                             bool repeatV = false) = 0;
    virtual Texture *newTextureFromImageData(image::ImageData *data,
                                             const TextureCreateInfo &info) = 0;
    virtual Texture *newTextureWithSampler(image::ImageData *data, bool repeatU, bool repeatV,
                                           bool generateMipmaps, float maxAnisotropy,
                                           const std::string &filter, const std::string &mipmap,
                                           float lodBias = 0.f) = 0;
    virtual void setTextureSamplerParams(Texture *texture, const std::string &filter,
                                         const std::string &mipmap, float maxAnisotropy,
                                         float lodBias) = 0;
    virtual void setTextureSampler(Texture *texture, const TextureSampler &sampler) = 0;
    virtual Texture *newTextureFromFile(const std::string &filename) = 0;
    virtual Texture *newTextureFromFileRepeated(const std::string &filename, bool repeatU,
                                                bool repeatV) = 0;
    virtual bool reloadTextureFromFile(const std::string &filename) = 0;
    virtual float getMaxAnisotropy() const = 0;
    virtual bool releaseTexture(Texture *texture) = 0;

    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh) = 0;
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) = 0;
    virtual Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                    int vertexCount, const uint32_t *indices, int indexCount) = 0;
    virtual bool updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ,
                                    const float *uvST, int vertexCount, const uint32_t *indices,
                                    int indexCount) = 0;
    virtual bool bakeMeshMorph(Mesh *mesh) = 0;
    virtual Mesh *newMeshSphere(int slices, int stacks) = 0;
    virtual Mesh *newMeshCylinder(int slices, int stacks, bool caps) = 0;
    virtual Mesh *newMeshCube(float size) = 0;
    virtual bool releaseMesh(Mesh *mesh) = 0;

    virtual Shader *newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                     const std::vector<uint32_t> &fragSpv) = 0;
    virtual Shader *newShaderFromSpvFile(const std::string &vertPath,
                                         const std::string &fragPath) = 0;
    virtual Shader *newShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;

    /**
     * @brief Create a 2D custom shader from WGSL source (WebGPU backend).
     * Empty vert → default textured vertex shader. The fragment WGSL declares
     * the shared 2D bindings (color texture 0, depth texture 1, sampler 2,
     * depth sampler 3, Externals UBO 4) and vs_main/fs_main entry points.
     * Vulkan throws (uses SPIR-V via newShaderFromSpv).
     */
    virtual Shader *newShaderFromWgsl(const std::string &vertWgsl,
                                      const std::string &fragWgsl) = 0;
    virtual Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    virtual Shader *newMeshShaderFromWgsl(const std::string &vertWgsl,
                                          const std::string &fragWgsl) = 0;
    virtual Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    virtual Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    virtual bool releaseShader(Shader *shader) = 0;

    virtual Canvas *newCanvas(int width, int height) = 0;
    virtual Font *newFont(font::FontData *data, std::string charset) = 0;
};

}  // namespace eve::graphics
