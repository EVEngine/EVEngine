#pragma once

// Narrow resource-factory interface of the Graphics backend.
// Consumers that only create textures / meshes / shaders / canvases (model3d,
// animation, avatar, sceneloader) depend on this instead of the full
// graphics::Graphics god class.

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "common/BorrowedRef.h"
#include "common/Result.h"

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
    /** @brief Releases IResourceFactory resources. */
    virtual ~IResourceFactory() = default;

    /** @brief Creates a texture. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba, bool repeatU = false,
                                bool repeatV = false) = 0;
    /** @brief Creates a texture. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba,
                                const TextureCreateInfo &info) = 0;
    /** @brief Upload complete explicit linear RGBA8 mips without regenerating pixels.
     * @param width Positive base width.
     * @param height Positive base height.
     * @param levels Full halving chain through 1x1, clamping each dimension to one.
     * @param rgba Borrowed tightly packed top-down levels in increasing LOD order; not retained.
     * @return Failure without publication or a factory-owned texture; default is Unsupported.
     * @lifetime Until shutdown or releaseTexture; successful release transfers the CPU facade.
     * @thread Graphics thread only, synchronous, non-reentrant and without callbacks.
     */
    [[nodiscard]] virtual Result<Texture *> newTextureMipChain(uint32_t width, uint32_t height, uint32_t levels,
                                                               std::span<const uint8_t> rgba) {
        (void)width;
        (void)height;
        (void)levels;
        (void)rgba;
        /** @brief Failure. */
        return Result<Texture *>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                            "explicit texture mip upload is unavailable", {}, {},
                                                            "graphics.texture.mips"));
    }
    /** @brief Upload a sampled 2D array of tightly packed linear RGBA16F texels.
     * @param rgbaHalf IEEE-754
     * binary16 channel bits, layer-major and row-major within each layer; borrowed for call.
     * @return Failure
     * without publication or a factory-owned texture. Unsupported is explicit on absent providers.
     * @thread
     * Graphics thread only, synchronous, non-reentrant and without callbacks.
     */
    [[nodiscard]] virtual Result<Texture *> newTextureArrayRgba16f(uint32_t width, uint32_t height, uint32_t layers,
                                                                   std::span<const uint16_t> rgbaHalf) {
        (void)width;
        (void)height;
        (void)layers;
        (void)rgbaHalf;
        /** @brief Failure. */
        return Result<Texture *>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "RGBA16F texture arrays are unavailable", {}, {}, "graphics.texture.array"));
    }
    /** @brief Upload one tightly packed linear RGBA8 3D sampled texture.
     * @param width Positive X extent.
     *
     * @param height Positive Y extent.
     * @param depth Positive Z extent.
     * @param rgba Borrowed Z-major
     * slices, each row-major RGBA8; not retained.
     * @return Failure without publication or a factory-owned
     * texture; default is Unsupported.
     * @lifetime Until shutdown or releaseTexture; successful release transfers
     * the CPU facade.
     * @thread Graphics thread only, synchronous, non-reentrant and without callbacks.
     */
    [[nodiscard]] virtual Result<Texture *> newTexture3DRgba8(uint32_t width, uint32_t height, uint32_t depth,
                                                              std::span<const uint8_t> rgba) {
        (void)width;
        (void)height;
        (void)depth;
        (void)rgba;
        /** @brief Failure. */
        return Result<Texture *>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "RGBA8 3D textures are unavailable", {}, {}, "graphics.texture.volume"));
    }
    /** @brief Creates a cubemap. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces) = 0;
    /** @brief Creates a cubemap. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces,
                                const TextureCreateInfo &info) = 0;
    /** @brief Creates a texture. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTexture(image::ImageData *data) = 0;
    /** @brief Creates a texture. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTexture(image::ImageData *data, const TextureCreateInfo &info) = 0;
    /**
     * @brief Reuse or upload immutable RGBA8 pixels under a caller-provided content key.
     * @param contentKey Stable, collision-resistant key for the complete pixel payload and dimensions.
     * @param data Borrowed image consumed synchronously when the key is not already resident.
     * @return Structured result borrowing the factory-owned texture; Unsupported without a caching provider.
     * @ownership The factory owns the returned texture.
     * @lifetime Until releaseTexture or graphics shutdown.
     * @thread Graphics thread only; no callbacks are invoked.
     * @cost First use performs one texture upload; cache hits are hash-table lookups.
     */
    [[nodiscard]] virtual ResultRef<Texture> newSharedTexture(image::ImageData *data, const std::string &contentKey) {
        (void)data;
        (void)contentKey;
        return ResultRef<Texture>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                             "immutable texture sharing is unavailable", {}, {},
                                                             "graphics.texture.shared"));
    }
    /** @brief Creates a texture from image data. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTextureFromImageData(image::ImageData *data, bool repeatU = false,
                                             bool repeatV = false) = 0;
    /** @brief Creates a texture from image data. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTextureFromImageData(image::ImageData *data,
                                             const TextureCreateInfo &info) = 0;
    /** @brief Creates a texture with sampler. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTextureWithSampler(image::ImageData *data, bool repeatU, bool repeatV,
                                           bool generateMipmaps, float maxAnisotropy,
                                           const std::string &filter, const std::string &mipmap,
                                           float lodBias = 0.f) = 0;
    /** @brief Set desired sampler state; equal state is a no-op and repeated edits coalesce.
     * @thread Graphics thread, non-reentrant; inputs copied, no callbacks.
     * @details Published before the next consuming draw. Releasing a texture drops pending state.
     * @cost Equal setter is constant-time; changed state checks factory ownership; publication may create a cached
     * sampler and drain in-flight descriptor use once per batch on Vulkan. Keep continuously varying sampler state out
     * of hot loops. */
    virtual void setTextureSampler(Texture *texture, const TextureSampler &sampler) = 0;
    /** @brief Load a path-cached file texture; cache hits return the same resource without I/O or upload.
     * @ownership Graphics owns the returned facade; releaseTexture transfers it to the caller.
     * @lifetime Until explicit release or graphics shutdown. Store the resource instead of loading per frame.
     * @thread Graphics thread, no callbacks. Legacy pointer/exception projection of the resource provider.
     * @cost Cache miss requests decode; GPU realization occurs at the first consuming draw.
     * Cache hit performs path lookup only. Reload is explicit through reloadTextureFromFile. */
    virtual Texture *newTextureFromFile(const std::string &filename) = 0;
    /** @brief Load a ready path-cached texture without requiring caller-managed tasks.
     * @param filename File path consumed synchronously; not retained as a borrowed string.
     * @return Factory-owned resident texture or an explicit diagnostic; default provider is Unsupported.
     * @ownership Borrowed result; factory retains ownership.
     * @lifetime Until releaseTexture or factory destruction; reload preserves facade identity.
     * @thread Graphics owner thread; no callbacks.
     * @cost Named load: first use decodes/uploads before returning; resident hits only query the path cache.
     * Store the result for frame loops. Size queries on successful results perform no preparation. */
    [[nodiscard]] virtual ResultRef<Texture> loadTexture(const std::string &filename) {
        (void)filename;
        return ResultRef<Texture>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                             "resident file texture loading is unavailable", {}, {},
                                                             "graphics.texture.load"));
    }
    /** @brief Creates a texture from file repeated. @ownership Caller deletes unless documented otherwise. */
    virtual Texture *newTextureFromFileRepeated(const std::string &filename, bool repeatU,
                                                bool repeatV) = 0;
    /** @brief Reloads texture from file. */
    virtual bool reloadTextureFromFile(const std::string &filename) = 0;
    /** @brief Returns the max anisotropy. */
    virtual float getMaxAnisotropy() const = 0;
    /** @brief Release texture. */
    virtual bool releaseTexture(Texture *texture) = 0;

    /** @brief Creates a mesh from assimp. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh) = 0;
    /** @brief Creates a mesh from assimp. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) = 0;
    /** @brief Creates a mesh from arrays. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                    int vertexCount, const uint32_t *indices, int indexCount) = 0;
    /**
     * @brief Create a mesh with a packed linear RGBA vertex-color stream.
     * @return Factory-owned mesh, or nullptr when validation or backend allocation fails.
     * @ownership The resource factory owns the returned mesh; release it through releaseMesh.
     * @lifetime Valid until releaseMesh, graphics shutdown, or device loss.
     * @thread Graphics thread; all input arrays are borrowed and consumed synchronously.
     */
    virtual Mesh *newMeshFromArraysColored(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                           const float *colorRGBA, int vertexCount,
                                           const uint32_t *indices, int indexCount) = 0;
    /** @brief Updates mesh vertices.
     * @cost Linear conversion/upload per changed stream; amortize by submitting only changed geometry. */
    virtual bool updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ,
                                    const float *uvST, int vertexCount, const uint32_t *indices,
                                    int indexCount) = 0;
    /** @brief Bake mesh morph. */
    virtual bool bakeMeshMorph(Mesh *mesh) = 0;
    /** @brief Creates a mesh sphere. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshSphere(int slices, int stacks) = 0;
    /** @brief Creates a mesh cylinder. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshCylinder(int slices, int stacks, bool caps) = 0;
    /** @brief Creates a mesh cube. @ownership Caller deletes unless documented otherwise. */
    virtual Mesh *newMeshCube(float size) = 0;
    /** @brief Release mesh. */
    virtual bool releaseMesh(Mesh *mesh) = 0;

    /** @brief Creates a shader from spv. @ownership Caller deletes unless documented otherwise. */
    virtual Shader *newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                     const std::vector<uint32_t> &fragSpv) = 0;
    /** @brief Creates a shader from spv file. @ownership Caller deletes unless documented otherwise. */
    virtual Shader *newShaderFromSpvFile(const std::string &vertPath,
                                         const std::string &fragPath) = 0;
    /** @brief Creates a shader. @ownership Caller deletes unless documented otherwise. */
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
    /** @brief Create a Mesh3D shader; an empty stage selects the matching engine default. */
    virtual Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    /** @brief Create a WebGPU Mesh3D shader; an empty stage selects the matching engine default. */
    virtual Shader *newMeshShaderFromWgsl(const std::string &vertWgsl,
                                          const std::string &fragWgsl) = 0;
    /** @brief Creates a mesh shader. @ownership Caller deletes unless documented otherwise. */
    virtual Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    /** @brief Creates a hair shader from spv. @ownership Caller deletes unless documented otherwise. */
    virtual Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    /** @brief Release shader. */
    virtual bool releaseShader(Shader *shader) = 0;

    /** @brief Creates a canvas. @ownership Caller deletes unless documented otherwise. */
    virtual Canvas *newCanvas(int width, int height) = 0;
    /** @brief Creates a font. @ownership Caller deletes unless documented otherwise. */
    virtual Font *newFont(font::FontData *data, std::string charset) = 0;
};

}  // namespace eve::graphics
