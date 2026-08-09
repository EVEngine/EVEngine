#pragma once

#include "common/Module.h"
#include "graphics/Shader.h"
#include "graphics/Drawable.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/Font.h"
#include "graphics/Light.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

struct aiMesh;

namespace eve::graphics {

class Graphics : public Module, public Canvas {
public:
    Module_REG(Graphics);
    virtual ~Graphics() {}

    /**
	 * Resets the current color, background color, line style, and so forth.
	 **/
	void reset();

    /** Script-friendly wrappers (r,g,b[,a] floats — no Color type in Squirrel). */
    void clearScreen();
    void setBackgroundColorRGBA(float r, float g, float b, float a = 1.f);
    void drawSolidRectRGBA(float x, float y, float w, float h, float r, float g, float b,
                           float a = 1.f);
    void drawTexturedRectRGBA(Texture *texture, float x, float y, float w, float h, float r,
                              float g, float b, float a = 1.f);
    /** Upload RGBA8 ImageData; optional seamless repeat on U/V. Caller owns Texture*. */
    Texture *newTextureFromImageData(image::ImageData *data, bool repeatU = false,
                                     bool repeatV = false);

    virtual void present() = 0;

    /**
     * Bind to an existing native window (SDL_Window*) and create Vulkan device/swapchain.
     * Must be called after the window exists (SDL_WINDOW_VULKAN).
     **/
    virtual void initWithWindow(void *nativeWindow) = 0;

    /**
     * Sets the current graphics display viewport dimensions.
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

    /** Internal immediate-mode helper used by RenderSystem / Batcher. */
    virtual void drawSolidRect(float x, float y, float w, float h, const Color &color) = 0;

    /** Create RGBA8 texture from CPU pixels (size = width*height*4). Caller owns Texture*. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba, bool repeatU = false,
                                bool repeatV = false) = 0;

    /**
     * Create an RGBA8 cubemap from 6 faces packed as +X,-X,+Y,-Y,+Z,-Z
     * (each faceSize×faceSize, total bytes = faceSize²×4×6). Owned by Graphics.
     */
    virtual Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces) = 0;

    /** Create texture from ImageData (RGBA8 required for now). */
    virtual Texture *newTexture(image::ImageData *data) = 0;

    /** Load file via Filesystem + Image decode, then upload (RGBA8). Throws on failure.
     *  Same path returns the same Texture* and reloads pixels in place on repeat calls. */
    virtual Texture *newTextureFromFile(const std::string &filename) = 0;

    /** Reload a path-cached texture from disk in place (pointer stable). False if unbound. */
    virtual bool reloadTextureFromFile(const std::string &filename) = 0;

    /** Draw a textured quad (full UV 0..1). texture may be null → solid.
     *  Uses currentShader when set (or per-call override via drawTexturedRectShader). */
    virtual void drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                                  const Color &color) = 0;

    /** Draw with an explicit Shader (nullptr = default textured pipeline). */
    virtual void drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w,
                                        float h, const Color &color) = 0;

    /** Draw a textured sub-rect (atlas / tile UVs). texture may be null → solid. */
    virtual void drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0,
                                    float v0, float u1, float v1, const Color &color) = 0;

    /** UV draw with an explicit Shader (nullptr = default textured pipeline). */
    virtual void drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y,
                                          float w, float h, float u0, float v0, float u1, float v1,
                                          const Color &color) = 0;

    /**
     * Lit 2D draw (albedo + normal map). Uses Lighting2DUBO from setLighting2D.
     * normal may be null → treated as flat (0.5,0.5,1) only if a default normal tex exists.
     */
    virtual void drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                       float h, float u0, float v0, float u1, float v1,
                                       const Color &color) = 0;

    /** Upload per-frame / per-canvas 2D lighting constants for subsequent lit draws. */
    virtual void setLighting2D(const Lighting2DUBO &ubo) = 0;

    /** Pixel-space atlas rect. Squirrel owns the Quad*. */
    Quad *newQuad(int x, int y, int w, int h);

    /** Upload triangulated mesh from Assimp (pos/normal/uv + indices). Owned by Graphics. */
    virtual Mesh *newMeshFromAssimp(const ::aiMesh &mesh) = 0;

    /**
     * Procedural UV sphere (radius 1, Y-up). Owned by Graphics.
     * slices = longitude divisions, stacks = latitude divisions.
     */
    virtual Mesh *newMeshSphere(int slices = 32, int stacks = 16) = 0;

    /**
     * Procedural Y-up cylinder (radius 1, height 2 centered at origin).
     * slices = longitude divisions; stacks = height bands; caps = include end discs.
     * Owned by Graphics.
     */
    virtual Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) = 0;

    /** Run RenderSystem3D (begin3DFrame + draw visible Renderable3D). */
    void render3D();
    void setDirectionalLight(float dx, float dy, float dz, float r = 1.f, float g = 1.f,
                             float b = 1.f);

/**
 * Begin a swapchain pass cleared for 3D (color+depth). Leaves the pass open for drawMesh
 * and a following RenderSystem::render (2D). Does not present.
 * Soft-fails (no throw) when the swapchain cannot be acquired yet — check had3DThisFrame().
 */
virtual void begin3DFrame() = 0;

    /** viewProj used by subsequent drawMesh (mvp = viewProj * model). RH + ZO clip. */
    virtual void setMesh3DViewProj(const glm::mat4 &viewProj) = 0;

    /** Draw one mesh with model matrix. Requires begin3DFrame() (or an open swapchain pass). */
    virtual void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) = 0;

    /** Draw mesh with an explicit Mesh3D Shader (nullptr = default PBR pipeline). */
    virtual void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                                Shader *shader) = 0;

    /** Optional normal map for the next drawMesh / drawMeshShader (nullptr = flat). */
    virtual void setMesh3DNormalTexture(Texture *normal) = 0;

    /** Metallic (0..1) and roughness (0..1) for the next default mesh draw. */
    virtual void setMesh3DMaterial(float metallic, float roughness) = 0;

    /** Per-frame ambient + up to 8 lights packed into Mesh3DUBO. */
    virtual void setMesh3DLighting(const Lighting3DPack &pack) = 0;

    /**
     * Enable clustered forward path for subsequent default mesh draws (SSBO light lists).
     * Pass upload.active=false (or empty) to disable and return to the ≤8 UBO path.
     */
    virtual void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) = 0;

    /** Directional light for subsequent drawMesh calls (world-space direction toward surface). */
    virtual void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) = 0;

    /** Camera eye used by mesh shaders that need view/rim (stored in Mesh3DUBO). */
    virtual void setMesh3DCameraPos(const glm::vec3 &eye) = 0;

    /**
     * Specular IBL environment for subsequent default mesh draws.
     * cube must be from newCubemap (or nullptr → black / intensity 0).
     * intensity is packed into Mesh3DUBO lightColor.w.
     */
    virtual void setMesh3DEnv(Texture *cube, float intensity) = 0;

    /** Upload CSM constants for subsequent default mesh draws (active=false disables). */
    virtual void setMesh3DShadows(const ShadowUpload &upload) = 0;

    /** Per-draw: when false, shadow sampling is forced off for the next mesh draw. */
    virtual void setMesh3DShadowReceive(bool receive) = 0;

    /**
     * Depth-only shadow pass for one cascade layer (0..2). Uses a one-shot submit;
     * call before begin3DFrame.
     */
    virtual void beginShadowPass(int cascadeIndex) = 0;
    virtual void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) = 0;
    virtual void endShadowPass() = 0;

    /** True after begin3DFrame until present completes. */
    bool consumeFrameHad3D() {
        bool v = frameHad3D;
        return v;
    }
    bool had3DThisFrame() const { return frameHad3D; }

    Color getBackgroundColor() const { return backgroundColor; }
    void setBackgroundColor(const Color &c) { backgroundColor = c; }

    /**
     * When true, each present copies the swapchain to a CPU buffer for getPixel/newImageData.
     * Default false — full-frame readback is expensive; enable only for tests / tools.
     */
    void setScreenReadbackEnabled(bool enabled) { screenReadbackEnabled = enabled; }
    bool isScreenReadbackEnabled() const { return screenReadbackEnabled; }

    /** Pause/resume presenting (Android background / foreground). */
    void setActive(bool active) {
        graphicsActive = active;
        if (active)
            markSwapchainDirty();
    }
    bool isActive() const { return graphicsActive; }

    /** Request swapchain recreation on next present/begin3DFrame. */
    virtual void markSwapchainDirty() {}

    /**
     * Request recreation of the platform render surface on the next frame
     * (Android background/foreground destroys the native window). Safe to call
     * from a non-render thread; the actual work happens on the render thread.
     */
    virtual void requestSurfaceRecreate() {}

    /**
     * Optional overlay drawn inside the swapchain render pass (before end).
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
     * Build a GPU font (glyph atlas texture) from decoded font data.
     * Rasterizes `charset` (UTF-8, default: printable ASCII) up front;
     * codepoints outside it still advance in print() but aren't drawn.
     * Owned by the caller (same convention as newTexture, newMesh, newShader).
     */
    Font *newFont(font::FontData *data, std::string charset = Font::defaultCharset());

    /** Font used by subsequent print() calls; nullptr = none set. */
    void setFont(Font *font) { currentFont = font; }
    Font *getFont() const { return currentFont; }

    /**
     * Draws UTF-8 `text` with the current font (see setFont), baseline-aligned
     * so that (x,y) is the top-left of the line. Throws if no font is set.
     */
    void print(const std::string &text, float x, float y, const Color &color = Color(1.f, 1.f, 1.f, 1.f),
               float scale = 1.f);

	void setShader(Shader *shader);
	void setShader();

	Shader *getShader() const { return currentShader; }

    /**
     * Create a custom 2D shader from SPIR-V words (vert + frag).
     * Owned by Graphics. Vertex stage may be empty → uses the default textured vertex shader.
     */
    virtual Shader *newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                    const std::vector<uint32_t> &fragSpv) = 0;

    /** Load SPIR-V from files via Filesystem (empty vertPath → default textured vert). */
    virtual Shader *newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) = 0;
    Shader *newShaderFromSpvFile(const std::string &fragPath) {
        return newShaderFromSpvFile(std::string(), fragPath);
    }

    /**
     * Compile GLSL source with glslc (must be on PATH). Empty vertGlsl → default textured vert.
     * Throws if compilation fails.
     */
    virtual Shader *newShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    Shader *newShader(const std::string &fragGlsl) { return newShader(std::string(), fragGlsl); }

    /**
     * Create a Mesh3D custom shader (MeshVertex + Frame UBO + albedo).
     * Empty vert → default mesh3d.vert. Owned by Graphics.
     */
    virtual Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                         const std::vector<uint32_t> &fragSpv) = 0;
    virtual Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) = 0;
    Shader *newMeshShader(const std::string &fragGlsl) {
        return newMeshShader(std::string(), fragGlsl);
    }

    /** Create an offscreen render target (sampleable). Owned by Graphics. */
    virtual Canvas *newCanvas(int width, int height) = 0;

    /** nullptr or this → screen. Switching flushes pending draws to the previous target. */
    virtual void setCanvas(Canvas *canvas) = 0;
    void setCanvas() { setCanvas(nullptr); }

    virtual bool isCanvasActive() const = 0;
    virtual Canvas *getCanvas() const = 0;

    void draw(Drawable *drawable, const glm::mat4 &m);
	// void draw(Texture *texture, Quad *quad, const glm::mat4 &m);
	// void drawLayer(Texture *texture, int layer, const glm::mat4 &m);
	// void drawLayer(Texture *texture, int layer, Quad *quad, const glm::mat4 &m);
	// void drawInstanced(Mesh *mesh, const glm::mat4 &m, int instancecount);


	/**
	 * Draws a series of points at the specified positions.
	 **/
	void points(const std::vector<glm::vec2>& positions, const std::vector<Color>& colors);

	/**
	 * Draws a series of lines connecting the given vertices.
	 * @param coords Vertex positions (v1, ..., vn). If v1 == vn the line will be drawn closed.
	 * @param count Number of vertices.
	 **/
	void polyline(const glm::mat4 *vertices, size_t count);

	/**
	 * Draws a rectangle.
	 * @param x Position along x-axis for top-left corner.
	 * @param y Position along y-axis for top-left corner.
	 * @param w The width of the rectangle.
	 * @param h The height of the rectangle.
	 **/
	void rectangle(std::string mode, float x, float y, float w, float h);

	/**
	 * Variant of rectangle that draws a rounded rectangle.
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
	 * Draws a circle using the specified arguments.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate.
	 * @param y Y-coordinate.
	 * @param radius Radius of the circle.
	 * @param points Number of points to use to draw the circle.
	 **/
	void circle(std::string mode, float x, float y, float radius, int points);
	void circle(std::string mode, float x, float y, float radius);

	/**
	 * Draws an ellipse using the specified arguments.
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
	 * Draws an arc using the specified arguments.
	 * @param drawmode The mode of drawing (line/filled).
	 * @param arcmode The type of arc.
	 * @param x X-coordinate.
	 * @param y Y-coordinate.
	 * @param radius Radius of the arc.
	 * @param angle1 The angle at which the arc begins.
	 * @param angle2 The angle at which the arc terminates.
	 * @param points Number of points to use to draw the arc.
	 **/
	void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2, int points);
	void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2);

	/**
	 * Draws a polygon with an arbitrary number of vertices.
	 * @param mode The type of drawing (line/filled).
	 * @param coords Vertex positions.
	 * @param count Vertex array size.
	 **/
	void polygon(std::string mode, const std::vector<glm::vec2>& vertices, bool skipLastFilledVertex = true);


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
    bool screenReadbackEnabled = false;
    bool graphicsActive = true;
    PresentOverlayFn presentOverlayFn_ = nullptr;
    void *presentOverlayUser_ = nullptr;
    Shader *currentShader = nullptr;
    Font *currentFont = nullptr;
};

}  // namespace eve::graphics
