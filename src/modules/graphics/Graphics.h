#pragma once

#include <assimp/matrix4x4.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "common/Module.h"
#include "common/WindowSurfaceHost.h"
#include "graphics/BlendMode.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Font.h"
#include "graphics/IGraphics2D.h"
#include "graphics/IGraphics3D.h"
#include "graphics/IPostFX.h"
#include "graphics/IResourceFactory.h"

struct aiMesh;

namespace eve::graphics {

class AmbientOcclusion;
class AntiAliasing;
class Camera3D;
class Drawable;
class GBuffer;
class GlobalIllumination;
class GrassField;
class Material;
class Mesh;
class Outline;
class Quad;
class RenderControl;
class ScreenSpaceReflection;
class Shader;
class Texture;
class Volumetric;
class Water;
class Waterfall;
struct ClusteredLightingUpload;
struct Lighting2DUBO;
struct Lighting3DPack;
struct ShadowUpload;
struct TextureCreateInfo;
struct TextureSampler;

class Graphics : public Module,
                 public Canvas,
                 public IWindowSurfaceHost,
                 public IGraphics2D,
                 public IGraphics3D,
                 public IResourceFactory,
                 public IPostFX {
public:
    Module_REG(Graphics);
    Graphics();
    ~Graphics() override;

    /**
	 * @brief Resets the current color, background color, line style, and so forth.
	 **/
    virtual void reset();

    /** @brief Script-friendly wrappers (r,g,b[,a] floats — no Color type in Squirrel). */
    virtual void clearScreen();
    virtual void setBackgroundColorRGBA(float r, float g, float b, float a = 1.f);
    virtual void drawSolidRectRGBA(float x, float y, float w, float h, float r, float g, float b, float a = 1.f);
    virtual void drawTexturedRectRGBA(Texture *texture, float x, float y, float w, float h, float r, float g, float b,
                                      float a = 1.f);
    /** Upload RGBA8 ImageData; optional seamless repeat on U/V.
     *  Borrowed handle: Graphics owns the texture (freed at shutdown or via
     *  releaseTexture); callers must not delete it. */
    Texture *newTextureFromImageData(image::ImageData *data, bool repeatU = false,
                                     bool repeatV = false);
    /** @brief Upload RGBA8 ImageData with mipmaps / filter / anisotropy options. */
    Texture *newTextureFromImageData(image::ImageData *data, const TextureCreateInfo &info);

    /**
     * @brief Script-friendly texture create: filter = "linear"|"nearest", mipmap = "none"|"linear"|"nearest".
     * generateMipmaps builds a full mip chain; maxAnisotropy > 1 enables anisotropic filtering.
     */
    Texture *newTextureWithSampler(image::ImageData *data, bool repeatU, bool repeatV,
                                   bool generateMipmaps, float maxAnisotropy,
                                   const std::string &filter, const std::string &mipmap,
                                   float lodBias = 0.f);

    /** @brief Update sampler state without re-uploading pixels (filter / mip / aniso / LOD bias). */
    virtual void setTextureSamplerParams(Texture *texture, const std::string &filter, const std::string &mipmap,
                                         float maxAnisotropy, float lodBias);

    virtual void present() = 0;

    /** @brief Renderer backend id used by sibling modules (e.g. Gpgpu). */
    virtual std::string getBackendName() const = 0;

    /**
     * @brief Whether gbuffer-based post-process shaders (AO, GI) can be created on this
     * backend. False on WebGPU, whose custom post shaders are WGSL-only (the
     * built-in AO/GI use SPIR-V), so RenderSystem3D skips them there.
     */
    virtual bool supportsGBufferPost() const { return true; }

    /**
     * @brief Bind to an existing native window (SDL_Window*) and create Vulkan device/swapchain.
     * Must be called after the window exists (SDL_WINDOW_VULKAN).
     **/
    virtual void initWithWindow(void *nativeWindow) = 0;

    /**
     * @brief Sets the current graphics display viewport dimensions.
     **/
    virtual void setViewportSize(int width, int height, int pixelwidth, int pixelheight) = 0;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getPixelWidth() const { return pixelWidth; }
    int getPixelHeight() const { return pixelHeight; }

    double getCurrentDPIScale() const {
        return (width > 0) ? double(pixelWidth) / double(width) : 1.0;
    }
    double getScreenDPIScale() const { return getCurrentDPIScale(); }

    /** @brief Internal immediate-mode helper used by RenderSystem / Batcher. */
    virtual void drawSolidRect(float x, float y, float w, float h, const Color &color,
                               BlendMode blend = BlendMode::Alpha) = 0;

    /** @brief Rotated solid quad `degrees` clockwise (screen Y-down) around (cx, cy). */
    virtual void drawSolidRectRotated(float cx, float cy, float w, float h, float degrees,
                                      const Color &color,
                                      BlendMode blend = BlendMode::Alpha) = 0;

    /** Create RGBA8 texture from CPU pixels (size = width*height*4).
     *  Borrowed handle: Graphics owns the texture (freed at shutdown or via
     *  releaseTexture); callers must not delete it. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba, bool repeatU = false,
                                bool repeatV = false) = 0;

    /** Create RGBA8 texture with explicit sampler / mipmap options.
     *  Borrowed handle: Graphics owns the texture (freed at shutdown or via
     *  releaseTexture); callers must not delete it. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba,
                                const TextureCreateInfo &info) = 0;

    /**
     * @brief Create an RGBA8 cubemap from 6 faces packed as +X,-X,+Y,-Y,+Z,-Z
     * (each faceSize×faceSize, total bytes = faceSize²×4×6). Owned by Graphics.
     */
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces) = 0;

    /** @brief Cubemap with mipmap / sampler options (IBL-friendly when generateMipmaps=true). */
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces,
                                const TextureCreateInfo &info) = 0;

    /** @brief Create texture from ImageData (RGBA8 required for now). */
    virtual Texture *newTexture(image::ImageData *data) = 0;

    /** @brief Create texture from ImageData with sampler / mipmap options. */
    virtual Texture *newTexture(image::ImageData *data, const TextureCreateInfo &info) = 0;

    /**
     * @brief Recreate the sampler for an existing texture (keeps image / mip chain).
     * No-op when texture is null or not owned by this Graphics.
     */
    virtual void setTextureSampler(Texture *texture, const TextureSampler &sampler) = 0;

    /** @brief Device max supported anisotropy (1 if unsupported). Valid after initWithWindow. */
    virtual float getMaxAnisotropy() const = 0;

    /** Load file via Filesystem + Image decode, then upload (RGBA8). Throws on failure.
     *  Same path returns the same Texture* and reloads pixels in place on repeat calls. */
    virtual Texture *newTextureFromFile(const std::string &filename) = 0;
    /** Load a texture from disk with wrap/repeat sampling (for tiling structures).
     *  Non-virtual helper (same pattern as newTextureFromImageData); reads + decodes
     *  via Filesystem/Image then uploads with the requested repeat modes. */
    Texture *newTextureFromFileRepeated(const std::string &filename, bool repeatU, bool repeatV);

    /** @brief Reload a path-cached texture from disk in place (pointer stable). False if unbound. */
    virtual bool reloadTextureFromFile(const std::string &filename) = 0;

    /**
     * @brief Eagerly releases a texture created by this Graphics.
     *
     * All newTexture* / newCubemap results are borrowed handles: the Graphics
     * backend owns both the CPU facade and the GPU resource, and the handle
     * must not be deleted directly. A successful release frees the GPU
     * resource (after in-flight frames drain), detaches the handle
     * (gpuHandle becomes null) and transfers the CPU facade to the caller,
     * who may then delete it. Renderer-owned fallback textures (white /
     * flat-normal / environment) and resources created by another Graphics
     * are never released here.
     *
     * @param texture Handle returned by a previous create call (may be null).
     * @return true when released; false for null, foreign, internal or
     *         already-released handles.
     */
    virtual bool releaseTexture(Texture *texture) {
        (void)texture;
        return false;
    }

    /** Draw a textured quad (full UV 0..1). texture may be null → solid.
     *  Uses currentShader when set (or per-call override via drawTexturedRectShader). */
    virtual void drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                                  const Color &color) = 0;

    /** @brief Draw with an explicit Shader (nullptr = default textured pipeline). */
    virtual void drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w,
                                        float h, const Color &color) = 0;

    /** @brief Draw a textured sub-rect (atlas / tile UVs). texture may be null → solid. */
    virtual void drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0,
                                    float v0, float u1, float v1, const Color &color) = 0;

    /** @brief UV draw with an explicit Shader (nullptr = default textured pipeline). */
    virtual void drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y,
                                          float w, float h, float u0, float v0, float u1, float v1,
                                          const Color &color, bool rotatedUV = false,
                                          BlendMode blend = BlendMode::Alpha) = 0;

    /**
     * @brief UV draw rotated `degrees` clockwise (screen Y-down) around the rect center.
     * texture may be null → solid. Shader nullptr = default textured pipeline.
     */
    virtual void drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx,
                                                 float cy, float w, float h, float degrees,
                                                 float u0, float v0, float u1, float v1,
                                                 const Color &color, bool rotatedUV = false,
                                                 BlendMode blend = BlendMode::Alpha) = 0;

    /**
     * @brief Fullscreen/post draw sampling `color` at binding 0 and `depth` at binding 1
     * (hardware D32, .r = Vulkan NDC z). depth may be null → color is bound twice.
     */
    virtual void drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader,
                                             float x, float y, float w, float h,
                                             const Color &tint) = 0;

    /**
     * @brief Lit 2D draw (albedo + normal map). Uses Lighting2DUBO from setLighting2D.
     * normal may be null → treated as flat (0.5,0.5,1) only if a default normal tex exists.
     */
    virtual void drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                       float h, float u0, float v0, float u1, float v1,
                                       const Color &color) = 0;

    /** @brief Upload per-frame / per-canvas 2D lighting constants for subsequent lit draws. */
    virtual void setLighting2D(const Lighting2DUBO &ubo) = 0;

    /** @brief Pixel-space atlas rect. Caller owns Quad* (not tracked by Graphics). */
    Quad *newQuad(int x, int y, int w, int h);

    /** Upload triangulated mesh from Assimp (pos/normal/uv + indices). Owned by Graphics.
     *  Also captures Assimp morph targets (aiAnimMesh) into Mesh CPU morph data when present. */
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh) = 0;

    /**
     * @brief Like newMeshFromAssimp, but bakes an Assimp node world transform into positions
     * and transforms normals by the inverse-transpose of the upper 3x3.
     * Required for hierarchical glTF/FBX scenes — raw aiMesh verts are in local space.
     */
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) = 0;

    /**
     * @brief Upload a triangle mesh from packed CPU arrays. Owned by Graphics.
     * posXYZ required (vertexCount*3). nrmXYZ/uvST may be null (flat normal / zero UV).
     * indices required (indexCount, triangles).
     */
    virtual Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                    int vertexCount, const uint32_t *indices, int indexCount) = 0;

    /**
     * @brief In-place update of a mesh's vertex/index data (CPU -> host-visible VBO).
     * Mirrors bakeMeshMorph: the update synchronizes with in-flight GPU work,
     * so prefer rebuilding only when content actually changes. The mesh's
     * buffer is reused while it fits (stable GPU handle) and reallocated when
     * the new size grows. Returns false when unsupported (WebGPU backend).
     * posXYZ/nrmXYZ follow newMeshFromArrays layout (uvST may be null);
     * indices/indexCount may be null/0 to keep the mesh's existing indices.
     */
    virtual bool updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ,
                                    const float *uvST, int vertexCount, const uint32_t *indices,
                                    int indexCount) = 0;

    /**
     * @brief If mesh morph weights are dirty, bake blended positions and upload to the GPU VBO.
     * Returns true when an upload happened. No-op when no morph data / not dirty / null.
     */
    virtual bool bakeMeshMorph(Mesh *mesh) = 0;

    /**
     * @brief Procedural UV sphere (radius 1, Y-up). Owned by Graphics.
     * slices = longitude divisions, stacks = latitude divisions.
     */
    virtual Mesh *newMeshSphere(int slices = 32, int stacks = 16) = 0;

    /**
     * @brief Procedural Y-up cylinder (radius 1, height 2 centered at origin).
     * slices = longitude divisions; stacks = height bands; caps = include end discs.
     * Owned by Graphics.
     */
    virtual Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) = 0;

    /**
     * @brief Procedural cube (edge length `size`, centered at origin, outward CCW for RH
     * Y-up), with per-face normals and a full 0..1 UV per face. Owned by Graphics.
     */
    Mesh *newMeshCube(float size = 1.f);

    /**
     * @brief Eagerly releases a mesh created by this Graphics.
     *
     * Mirrors releaseTexture: the returned handle is borrowed, a successful
     * release frees the GPU buffers, detaches the handle (gpuHandle becomes
     * null) and transfers the CPU facade to the caller for deletion.
     *
     * @param mesh Handle returned by a previous create call (may be null).
     * @return true when released; false for null, foreign or already-released
     *         handles.
     */
    virtual bool releaseMesh(Mesh *mesh) {
        (void)mesh;
        return false;
    }

    /** @brief Run RenderSystem3D (begin3DFrame + draw visible Renderable3D). */
    virtual void render3D();
    /**
     * Preview-quality 3D pass into an offscreen Canvas (editor viewport):
     * renders visible Renderable3D with `camera` into `canvas`, whose texture
     * can then be shown inside a UI Viewport widget. See RenderSystem3D::renderToCanvas.
     */
    virtual void renderScene3DToCanvas(Canvas *canvas, Camera3D *camera);
    virtual void setDirectionalLight(float dx, float dy, float dz, float r = 1.f, float g = 1.f, float b = 1.f);

    /**
     * @brief Composite this frame's 3D scene color into a rect (screen or active Canvas).
     virtual * Call after render3D(); order vs drawSolidRect / drawTexturedRect is preserved.
     * If never called, present() still blits the 3D scene fullscreen under 2D.
     * RGB is blitted opaque (scene A is linear depth, not transparency).
     */
    virtual void drawScene3DRGBA(float x, float y, float w, float h, float r = 1.f, float g = 1.f, float b = 1.f,
                                 float a = 1.f);
    /** @brief Script-friendly 4-arg form (simplesquirrel does not apply C++ defaults). */
    void drawScene3D(float x, float y, float w, float h) {
        drawScene3DRGBA(x, y, w, h, 1.f, 1.f, 1.f, 1.f);
    }

    /** @brief Draw a Canvas color buffer as a textured rect (same batch order as other 2D). */
    virtual void drawCanvasRGBA(Canvas *canvas, float x, float y, float w, float h, float r = 1.f, float g = 1.f,
                                float b = 1.f, float a = 1.f);
    void drawCanvas(Canvas *canvas, float x, float y, float w, float h) {
        drawCanvasRGBA(canvas, x, y, w, h, 1.f, 1.f, 1.f, 1.f);
    }

    /**
     * @brief Backend hooks so the platform-independent render3D() can wrap its work in
     * a GPU validation error scope (used on WebGPU to catch early device errors).
     */
    virtual void pushValidationScope() = 0;
    virtual void popValidationScope() = 0;

    /**
     * @brief Create a Material asset (shading model + textures + PBR knobs).
     * Caller owns Material*; not tracked by Graphics.
     */
    Material *newMaterial();

    /**
     * @brief Shared compilable 3D render control (features → pass list + GBuffer).
     * Owned by Graphics; valid for the module lifetime.
     */
    RenderControl *getRenderControl();

    /**
     * @brief Depth/normal(/albedo) fill pass for mid/post effects.
     * One-shot submit (like shadow); call before begin3DFrame when enabled.
     * After endGBufferPass, textures are available via getRenderControl()->getGBuffer().
     */
    virtual void beginGBufferPass(int width, int height) = 0;
    virtual void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                 float nearZ, float farZ, Texture *albedo = nullptr,
                                 float tintR = 1.f, float tintG = 1.f, float tintB = 1.f) = 0;
    /**
     * @brief GBuffer fill with alpha-cutout discard (card/billboard geometry such as
     * sprite-stack slices): same outputs as drawMeshGBuffer, but transparent
     * texels are discarded so depth/normal follow the silhouette. No-op on
     * backends without the alpha pipeline (WebGPU).
     */
    virtual void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                      float nearZ, float farZ, Texture *albedo = nullptr,
                                      float tintR = 1.f, float tintG = 1.f,
                                      float tintB = 1.f) = 0;
    virtual void endGBufferPass() = 0;

    /**
     * @brief Begin a 3D frame: shadow/gbuffer (if pending) then a sampleable scene color
     * pass (color+depth). Leaves the pass open for drawMesh and a following
     * RenderSystem::render (2D). Does not present. flushToSwapchain resolves the
     * scene color (FXAA when "aa" is on) into the swapchain, then draws 2D overlays.
     * Soft-fails (no throw) when the swapchain cannot be acquired yet — check had3DThisFrame().
     */
    virtual void begin3DFrame() = 0;

    /**
     * @brief Open a 3D render pass targeting an offscreen Canvas (color + depth) at
     * the canvas size. Uses setMesh3DViewProj/View/CameraPos/Env as the camera.
     * Draw meshes with drawMeshShader, then call end3DFrameToCanvas(). The
     * canvas texture then holds the rendered scene (e.g. a planar reflection).
     * Unsupported if `canvas` is not an offscreen canvas (screen).
     */
    virtual void begin3DFrameToCanvas(Canvas *canvas) = 0;
    virtual void end3DFrameToCanvas() = 0;

    /** viewProj used by subsequent drawMesh (mvp = viewProj * model).
     *  Expect RH + ZO with Vulkan NDC Y (see perspectiveVulkanRH_ZO). */
    virtual void setMesh3DViewProj(const glm::mat4 &viewProj) = 0;

    /** @brief Camera view matrix for subsequent drawMesh (view-space depth / CSM select). */
    virtual void setMesh3DView(const glm::mat4 &view) = 0;

    /** @brief Near/far used to pack linear depth into scene color A (SSGI). */
    virtual void setMesh3DClip(float nearZ, float farZ) = 0;

    /**
     * @brief Sampleable 3D color target for the current frame (RGB = lit, A = linear depth).
     * Valid after begin3DFrame until present; nullptr when 3D did not run offscreen.
     */
    virtual Texture *getSceneColorTexture() { return nullptr; }

    /**
     * @brief Per-pixel mesh entity-ID pass. Renders each EntityIdDraw's mesh with the
     * given flat idColor (RGB encodes a stable entity id) into an offscreen
     * target using the same viewProj, then reads it back to CPU. Pixels not
     * covered by any entity are (0,0,0,0). Caller owns the returned ImageData*.
     * Returns nullptr when the backend does not support offscreen ID capture.
     */
    struct EntityIdDraw {
        Mesh *mesh = nullptr;
        glm::mat4 model{1.f};
        glm::vec4 idColor{0.f, 0.f, 0.f, 1.f};
    };
    virtual image::ImageData *renderEntityIdMask(const std::vector<EntityIdDraw> &draws,
                                                 const glm::mat4 &viewProj, int width, int height) {
        (void)draws;
        (void)viewProj;
        (void)width;
        (void)height;
        return nullptr;
    }

    /**
     * @brief Read a G-buffer attachment back to CPU as RGBA8. name is one of
     * "depth" (RGBA8 linear depth), "normal" (world normal*0.5+0.5), "albedo".
     * Valid only after a G-buffer or entity-ID offscreen pass filled it.
     * Caller owns the returned ImageData*. Returns nullptr when unsupported.
     */
    virtual image::ImageData *readGBufferToImageData(const std::string &name) {
        (void)name;
        return nullptr;
    }

    /** @brief Draw one mesh with model matrix. Requires begin3DFrame() (or an open swapchain pass). */
    virtual void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) = 0;

    /** @brief Draw mesh with an explicit Mesh3D Shader (nullptr = default PBR pipeline). */
    virtual void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                                Shader *shader) = 0;

    /** @brief Optional normal map for the next drawMesh / drawMeshShader (nullptr = flat). */
    virtual void setMesh3DNormalTexture(Texture *normal) = 0;

    /** @brief Optional height map for parallax (R channel; nullptr = flat / off). */
    virtual void setMesh3DHeightTexture(Texture *height) = 0;

    /**
     * @brief Optional scene hardware depth (G-buffer hwDepth, Vulkan NDC z) bound to
     * mesh3d shader binding 7. X-ray mesh shaders sample it to discard visible
     * (non-occluded) fragments. nullptr falls back to a placeholder.
     */
    virtual void setMesh3DSceneDepth(Texture *depth) = 0;

    /** @brief Metallic (0..1) and roughness (0..1) for the next default mesh draw. */
    virtual void setMesh3DMaterial(float metallic, float roughness) = 0;

    /**
     * @brief Texture cell bombing for the next default mesh draw (breaks tiling).
     * cellScale: cells per UV unit (typical 2..16). strength: 0=off, 1=full.
     * rotAmount: 0..1 per-cell rotation scale (default 1).
     */
    virtual void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) = 0;

    /**
     * @brief Parallax occlusion mapping for the next default mesh draw.
     * scale: UV displacement strength (0=off). Typical 0.02..0.08.
     * minLayers / maxLayers: adaptive POM ray-march steps (more when glancing).
     */
    virtual void setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) = 0;

    /** @brief Per-frame ambient + up to 8 lights packed into Mesh3DUBO. */
    virtual void setMesh3DLighting(const Lighting3DPack &pack) = 0;

    /**
     * @brief Dynamic cloud shadows cast on the ground by the default PBR mesh path.
     * strength 0 disables (no change to rendering). time advances wind drift.
     * Packed into Mesh3DUBO.cloud / cloudWind and consumed by mesh3d shaders.
     */
    virtual void setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                                 float windAngle, float coverage, float detail) = 0;

    /**
     * @brief Enable clustered forward path for subsequent default mesh draws (SSBO light lists).
     * Pass upload.active=false (or empty) to disable and return to the ≤8 UBO path.
     */
    virtual void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) = 0;

    /** @brief Directional light for subsequent drawMesh calls (world-space direction toward surface). */
    virtual void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) = 0;

    /** @brief Camera eye used by mesh shaders that need view/rim (stored in Mesh3DUBO). */
    virtual void setMesh3DCameraPos(const glm::vec3 &eye) = 0;

    /**
     * @brief Instanced voxel face rectangles (32-bit packed instances).
     * ao: optional per-instance ambient-occlusion words (2 bits per corner,
     * 0..3, shader corner order); null → full bright.
     * faceDir: "posX"|"negX"|"posY"|"negY"|"posZ"|"negZ" (also "+x"/"-x"/…).
     * Requires begin3DFrame(); uses viewProj from setMesh3DViewProj.
     * atlas may be null → white; tilesPerRow subdivides atlas for texture indices.
     */
    virtual void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                        float originY, float originZ, const std::string &faceDir,
                                        Texture *atlas, int tilesPerRow = 16,
                                        const uint32_t *ao = nullptr) = 0;

    /**
     * @brief Specular IBL environment for subsequent default mesh draws.
     * cube must be from newCubemap (or nullptr → black / intensity 0).
     * intensity is packed into Mesh3DUBO lightColor.w.
     */
    virtual void setMesh3DEnv(Texture *cube, float intensity) = 0;

    /** @brief Upload CSM constants for subsequent default mesh draws (active=false disables). */
    virtual void setMesh3DShadows(const ShadowUpload &upload) = 0;

    /** @brief Per-draw: when false, shadow sampling is forced off for the next mesh draw. */
    virtual void setMesh3DShadowReceive(bool receive) = 0;

    /**
     * @brief Depth-only shadow pass for one cascade layer (0..2). Draws are recorded
     * into the next begin3DFrame command buffer (ping-pong map per frame slot).
     * Call before begin3DFrame.
     */
    virtual void beginShadowPass(int cascadeIndex) = 0;
    virtual void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) = 0;
    /**
     * @brief Shadow pass draw with alpha-cutout discard (card/billboard geometry such
     * as sprite-stack slices): transparent texels of `albedo` are discarded so
     * the slice casts a silhouette shadow instead of a solid quad. Requires an
     * active shadow pass (beginShadowPass). No-op on backends without the
     * alpha shadow pipeline (WebGPU).
     */
    virtual void drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP,
                                     Texture *albedo = nullptr) = 0;
    virtual void endShadowPass() = 0;

    /** @brief True after begin3DFrame until present completes. */
    bool consumeFrameHad3D() {
        bool v = frameHad3D;
        return v;
    }
    bool had3DThisFrame() const { return frameHad3D; }

    Color getBackgroundColor() const { return backgroundColor; }
    void setBackgroundColor(const Color &c) { backgroundColor = c; }

    /**
     * @brief When true, each present copies the swapchain to a CPU buffer for getPixel/newImageData.
     * Default false — full-frame readback is expensive; enable only for tests / tools.
     */
    virtual void setScreenReadbackEnabled(bool enabled) { screenReadbackEnabled = enabled; }
    bool isScreenReadbackEnabled() const { return screenReadbackEnabled; }

    /**
     * @brief Save the last presented frame to a PNG at @p path. Enables screen readback
     * if needed. Returns false if no presented frame is available or encoding fails.
     */
    bool saveFramePng(const std::string &path);

    /**
     * @brief Prefer uncapped present (IMMEDIATE/MAILBOX) when false, vsync (MAILBOX/FIFO)
     * when true. Takes effect on the next swapchain recreate.
     */
    virtual void setVSync(bool enabled) { vsyncEnabled = enabled; }
    bool isVSync() const { return vsyncEnabled; }

    /**
     * @brief Hardware MSAA sample count for the 3D scene color pass (0 disables, then
     * 2/4/8 are used when the device supports them). 0 or 1 mean no MSAA.
     * Takes effect on the next begin3DFrame.
     */
    virtual void setMsaaSamples(int samples) { msaaSamples = samples > 0 ? samples : 0; }
    virtual int getMsaaSamples() const { return msaaSamples; }

    /** @brief Pause/resume presenting (Android background / foreground). */
    void setActive(bool active) override {
        graphicsActive = active;
        if (active)
            markSwapchainDirty();
    }
    bool isActive() const { return graphicsActive; }

    /** @brief Request swapchain recreation on next present/begin3DFrame. */
    virtual void markSwapchainDirty() {}

    /**
     * @brief Request recreation of the platform render surface on the next frame
     * (Android background/foreground destroys the native window). Safe to call
     * from a non-render thread; the actual work happens on the render thread.
     */
    void requestSurfaceRecreate() override {}

    /**
     * @brief Called by the Window module when the native window backing the render
     * surface is destroyed (window close / recreation). Backends that keep a
     * platform surface tied to the native window must drop it here so the next
     * initWithWindow() rebuilds it against a fresh window — even when SDL hands
     * back the same pointer for the recreated window.
     *
     * The present overlay is bound to the destroyed window, so it is also
     * dropped here: a stale ImGui callback would otherwise keep drawing into
     * later presents of unrelated windows. Registered window-destroyed
     * callbacks (e.g. the UI backend's ImGui teardown) fire first so they can
     * release their ImGui context while the surface is still valid.
     */
    virtual void onNativeWindowDestroyed() {
        for (auto &cb : windowDestroyedCallbacks_) cb.first(cb.second);
        windowDestroyedCallbacks_.clear();
        clearPresentOverlay();
    }

    /** @brief Drop the present overlay callback (window gone; re-register on UI init). */
    void clearPresentOverlay() {
        presentOverlayFn_ = nullptr;
        presentOverlayUser_ = nullptr;
    }

    /**
     * @brief Register a callback invoked when the native window is destroyed.
     * Used by the UI backend to tear down its ImGui context; callbacks are
     * cleared (and invoked) on onNativeWindowDestroyed().
     */
    using WindowDestroyedCallback = void (*)(void *userdata);
    void addWindowDestroyedCallback(WindowDestroyedCallback cb, void *userdata) {
        windowDestroyedCallbacks_.emplace_back(cb, userdata);
    }

    /**
     * @brief Optional overlay drawn inside the swapchain render pass (before end).
     * Used by declarative UI (ImGui). `commandBuffer` is a VkCommandBuffer as void*.
     */
    using PresentOverlayFn = void (*)(void *userdata, void *commandBuffer);
    void setPresentOverlay(PresentOverlayFn fn, void *userdata) {
        presentOverlayFn_ = fn;
        presentOverlayUser_ = userdata;
    }
    PresentOverlayFn getPresentOverlay() const { return presentOverlayFn_; }
    void *getPresentOverlayUser() const { return presentOverlayUser_; }

    /**
     * @brief Build a GPU font (glyph atlas texture) from decoded font data.
     * Rasterizes `charset` (UTF-8, default: printable ASCII) up front;
     virtual * codepoints outside it still advance in print() but aren't drawn.
     * Caller owns Font* (not tracked by Graphics — unlike newTexture /
     * newMesh / newShader handles, which Graphics owns).
     */
    Font *newFont(font::FontData *data, std::string charset = Font::defaultCharset());

    /** @brief Font used by subsequent print() calls; nullptr = none set. */
    virtual void setFont(Font *font) { currentFont = font; }
    Font *getFont() const { return currentFont; }

    /**
     * @brief Draws UTF-8 `text` with the current font (see setFont), baseline-aligned
     * so that (x,y) is the top-left of the line. Throws if no font is set.
     */
    virtual void print(const std::string &text, float x, float y, const Color &color = Color(1.f, 1.f, 1.f, 1.f),
                       float scale = 1.f);

    virtual void setShader(Shader *shader);
    virtual void setShader();

    Shader *getShader() const { return currentShader; }

    /**
     * @brief Create a custom 2D shader from SPIR-V words (vert + frag).
     * Owned by Graphics. Vertex stage may be empty → uses the default textured vertex shader.
     */
    virtual Shader *newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                    const std::vector<uint32_t> &fragSpv) = 0;

    /** @brief Load SPIR-V from files via Filesystem (empty vertPath → default textured vert). */
    virtual Shader *newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) = 0;
    Shader *newShaderFromSpvFile(const std::string &fragPath) {
        return newShaderFromSpvFile(std::string(), fragPath);
    }

    /**
     * @brief Compile GLSL source with glslc (must be on PATH). Empty vertGlsl → default textured vert.
     * Throws if compilation fails.
     */
    virtual Shader *newShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    Shader *newShader(const std::string &fragGlsl) { return newShader(std::string(), fragGlsl); }

    /**
     * @brief Create a Mesh3D custom shader (MeshVertex + Frame UBO + albedo).
     * Empty vert → default mesh3d.vert. Owned by Graphics.
     */
    virtual Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    /**
     * @brief Create a Mesh3D custom shader from WGSL source (WebGPU backend).
     * The WGSL must declare the engine's Frame UBO (group 0 binding 0) and the
     * shared mesh3d bindings (albedo 1, normal 2, env 3, shadow UBO 4,
     * shadow depth 5, height 6, main sampler 7, shadow compare sampler 8,
     * scene depth 9). Vulkan throws (uses SPIR-V via newMeshShaderFromSpv).
     */
    virtual Shader *newMeshShaderFromWgsl(const std::string &vertWgsl,
                                          const std::string &fragWgsl) = 0;
    virtual Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    Shader *newMeshShader(const std::string &fragGlsl) {
        return newMeshShader(std::string(), fragGlsl);
    }

    /**
     * @brief Hair/fur card shader (alpha blend + Kajiya-Kay). Empty vert → mesh3d_hair.vert.
     * Owned by Graphics.
     */
    virtual Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    /** @brief Built-in hair shader with default anisotropic parameters. */
    Shader *newHairShader();

    /**
     * @brief Eagerly releases a shader created by this Graphics.
     *
     * Mirrors releaseTexture: the returned handle is borrowed, a successful
     * release destroys the GPU pipelines, detaches the handle (gpuHandle
     * becomes null) and transfers the CPU facade to the caller for deletion.
     * Only call this on shaders you created yourself; shaders owned by
     * Graphics pipeline objects (AA / AO / GI / grass / ...) are still reachable
     * through their owning objects and must not be released.
     *
     * @param shader Handle returned by a previous create call (may be null).
     * @return true when released; false for null, foreign or already-released
     *         handles.
     */
    virtual bool releaseShader(Shader *shader) {
        (void)shader;
        return false;
    }

    /**
     * @brief t3ssel8r-style grass billboard shader (alpha test + shadow two-tone).
     * Owned by Graphics. See grass:: / GrassField.
     */
    Shader *newGrassShader();

    /**
     * @brief Dense + sparse stylized grass field. Caller owns GrassField*;
     * its Mesh / Shader / Texture are owned by Graphics.
     */
    GrassField *newGrassField();

    /**
     * @brief Flowing waterfall (falling water sheet) with sky reflection, downward
     * velocity streaks and animated foam at the top lip and bottom splash pool.
     * Caller owns Waterfall*; its Mesh / Shader are owned by Graphics.
     */
    Waterfall *newWaterfall();

    /**
     * @brief Dynamic water surface (sky reflection + animated edge waves + middle
     * drop ripples). Caller owns Water*; its Mesh / Shader are owned by Graphics.
     */
    Water *newWater();

    /** @brief Create an offscreen render target (sampleable). Owned by Graphics. */
    virtual Canvas *newCanvas(int width, int height) = 0;

    /** @brief nullptr or this → screen. Switching flushes pending draws to the previous target. */
    virtual void setCanvas(Canvas *canvas) = 0;
    void setCanvas() { setCanvas(nullptr); }

    virtual bool isCanvasActive() const = 0;
    virtual Canvas *getCanvas() const = 0;

    /**
     * @brief Volumetric light + fog (screenspace / raymarch / fog). Caller owns Volumetric*;
     * its Shaders are owned by Graphics.
     */
    Volumetric *newVolumetric();

    /**
     * @brief Screen-space ambient occlusion (ssao / hbao / gtao). Caller owns AmbientOcclusion*;
     * its Shaders are owned by Graphics.
     */
    AmbientOcclusion *newAmbientOcclusion();

    /**
     * @brief Screen-space model outline (t3ssel8r-style) from GBuffer depth + normal.
     * Caller owns Outline*; its Shader is owned by Graphics.
     */
    Outline *newOutline();

    /**
     * @brief Screen-space single-bounce GI. Caller owns GlobalIllumination*;
     * its Shaders are owned by Graphics.
     */
    GlobalIllumination *newGlobalIllumination();

    /**
     * @brief Screen-space reflections (ray-marched over scene color + hw depth).
     * Caller owns ScreenSpaceReflection*; its Shader is owned by Graphics.
     */
    ScreenSpaceReflection *newScreenSpaceReflection();

    /**
     * @brief Pipeline-owned AO / GI / AA used by RenderSystem3D when features
     * "ao" / "gi" / "aa" are enabled. Created on first use; Graphics owns them.
     */
    AmbientOcclusion *pipelineAmbientOcclusion();
    GlobalIllumination *pipelineGlobalIllumination();
    AntiAliasing *pipelineAntiAliasing();
    /** @brief Pipeline-owned Outline used by RenderSystem3D when the "outline" feature is on. */
    Outline *pipelineOutline();

    /**
     * @brief Classic image-space AA (FXAA / SMAA / SSAA / NFAA). Caller owns AntiAliasing*;
     * its Shaders are owned by Graphics.
     */
    AntiAliasing *newAntiAliasing();

    virtual void draw(Drawable *drawable, const glm::mat4 &m);

    /**
     * @brief Volumetric occlusion helpers (shadow-pass analogue for light shafts).
     * drawOcclusion skips drawables with castOcclusion=false.
     */
    virtual void drawOcclusion(Drawable *drawable, const glm::mat4 &m);
    virtual void drawOcclusionSolid(float x, float y, float w, float h);
    virtual void drawOcclusionTexture(Texture *texture, float x, float y, float w, float h);
    // void draw(Texture *texture, Quad *quad, const glm::mat4 &m);
    // void drawLayer(Texture *texture, int layer, const glm::mat4 &m);
    // void drawLayer(Texture *texture, int layer, Quad *quad, const glm::mat4 &m);
    // void drawInstanced(Mesh *mesh, const glm::mat4 &m, int instancecount);


    /**
     * @brief Draws a series of points at the specified positions.
     **/
    void points(const std::vector<glm::vec2> &positions, const std::vector<Color> &colors);

    /**
     * @brief Draws a series of lines connecting the given vertices.
     * @param coords Vertex positions (v1, ..., vn). If v1 == vn the line will be drawn closed.
     * @param count Number of vertices.
     **/
    void polyline(const glm::mat4 *vertices, size_t count);

    /**
     * @brief Draws a rectangle.
     * @param x Position along x-axis for top-left corner.
     * @param y Position along y-axis for top-left corner.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     **/
    void rectangle(std::string mode, float x, float y, float w, float h);

    /**
     * @brief Variant of rectangle that draws a rounded rectangle.
     * @param mode The mode of drawing (line/filled).
     * @param x X-coordinate of top-left corner
     * @param y Y-coordinate of top-left corner
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     * @param rx The radius of the corners on the x axis
     * @param ry The radius of the corners on the y axis
     * @param points The number of points to use per corner
     **/
    void rectangle(std::string mode, float x, float y, float w, float h, float rx, float ry, int points);
    void rectangle(std::string mode, float x, float y, float w, float h, float rx, float ry);

    /**
     * @brief Draws a circle using the specified arguments.
     * @param mode The mode of drawing (line/filled).
     * @param x X-coordinate.
     * @param y Y-coordinate.
     * @param radius Radius of the circle.
     * @param points Number of points to use to draw the circle.
     **/
    void circle(std::string mode, float x, float y, float radius, int points);
    void circle(std::string mode, float x, float y, float radius);

    /**
     * @brief Draws an ellipse using the specified arguments.
     * @param mode The mode of drawing (line/filled).
     * @param x X-coordinate of center
     * @param y Y-coordinate of center
     * @param a Radius in x-direction
     * @param b Radius in y-direction
     * @param points Number of points to use to draw the circle.
     **/
    void ellipse(std::string mode, float x, float y, float a, float b, int points);
    void ellipse(std::string mode, float x, float y, float a, float b);

    /**
     * @brief Draws an arc using the specified arguments.
     * @param drawmode The mode of drawing (line/filled).
     * @param arcmode The type of arc.
     * @param x X-coordinate.
     * @param y Y-coordinate.
     * @param radius Radius of the arc.
     * @param angle1 The angle at which the arc begins.
     * @param angle2 The angle at which the arc terminates.
     * @param points Number of points to use to draw the arc.
     **/
    void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2,
             int points);
    void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2);

    /**
     * @brief Draws a polygon with an arbitrary number of vertices.
     * @param mode The type of drawing (line/filled).
     * @param coords Vertex positions.
     * @param count Vertex array size.
     **/
    void polygon(std::string mode, const std::vector<glm::vec2> &vertices, bool skipLastFilledVertex = true);


    void push(bool all);
	void pop();

	const glm::mat4 &getTransform() const;
	const glm::mat4 &getProjection() const;

	void rotate(float r);
	void scale(float x, float y = 1.0f);
	void translate(float x, float y);
	void origin();

	// void applyTransform(love::math::Transform *transform);
	// void replaceTransform(love::math::Transform *transform);

	glm::vec2 transformPoint(glm::vec2 point);
	glm::vec2 inverseTransformPoint(glm::vec2 point);

	// virtual void draw(const DrawCommand &cmd) = 0;
	// virtual void draw(const DrawIndexedCommand &cmd) = 0;
	// virtual void drawQuads(int start, int count, const vertex::Attributes &attributes, const vertex::BufferBindings &buffers, Texture *texture) = 0;

protected:
    int width = 0;
    int height = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    Color backgroundColor{0.1f, 0.1f, 0.12f, 1.0f};
    bool frameHad3D = false;
    /** @brief True while RenderSystem3D is submitting (AO / engine overlays). */
    bool recordingEngine3D_ = false;
    bool screenReadbackEnabled = false;
    bool vsyncEnabled = true;
    bool graphicsActive = true;
    int msaaSamples = 4;
    PresentOverlayFn presentOverlayFn_ = nullptr;
    void *presentOverlayUser_ = nullptr;
    std::vector<std::pair<WindowDestroyedCallback, void *>> windowDestroyedCallbacks_;
    Shader *currentShader = nullptr;
    Font *currentFont = nullptr;
    std::unique_ptr<RenderControl> renderControl_;
    std::unique_ptr<AmbientOcclusion> pipelineAO_;
    std::unique_ptr<GlobalIllumination> pipelineGI_;
    std::unique_ptr<AntiAliasing> pipelineAA_;
    std::unique_ptr<Outline> pipelineOutline_;

    /** @brief FXAA resolve shader that writes opaque RGB (ignores scene-color depth alpha). */
    Shader *prepareSceneColorResolveShader(Texture *scene);
};

}  // namespace eve::graphics
