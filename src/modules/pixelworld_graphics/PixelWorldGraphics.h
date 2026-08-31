#pragma once

#include "common/Module.h"

#include <cstdint>
#include <memory>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::pixelworld {
class PixelWorld;
}  // namespace eve::pixelworld

namespace eve::pixelworld_graphics {

/**
 * @brief Render-thread-affine RGBA atlas projection of a rectangular PixelWorld region.
 * @ownership The renderer owns only its CPU atlas; Graphics owns the Texture handle.
 * @lifetime A renderer using a texture must be destroyed before its Graphics backend.
 *
 * The renderer never owns or retains the PixelWorld/Graphics arguments supplied to
 * sync(). Its Texture is a borrowed backend-owned handle and must not outlive Graphics.
 * The CPU atlas is authoritative only for presentation and is rebuilt after world
 * revision rollback (clear/restore); gameplay continues to read PixelWorld itself.
 */
class PixelWorldAtlasRenderer {
public:
    /** @brief Create a transparent atlas for `[origin, origin + size)`. Throws on invalid size. */
    PixelWorldAtlasRenderer(int originX, int originY, int width, int height);
    ~PixelWorldAtlasRenderer();
    PixelWorldAtlasRenderer(PixelWorldAtlasRenderer&&) noexcept;
    PixelWorldAtlasRenderer& operator=(PixelWorldAtlasRenderer&&) noexcept;
    PixelWorldAtlasRenderer(const PixelWorldAtlasRenderer&) = delete;
    PixelWorldAtlasRenderer& operator=(const PixelWorldAtlasRenderer&) = delete;

    /**
     * @brief Patch changed CPU chunks and upload the atlas when its world revision advances.
     * @return Number of changed chunks patched; zero means no GPU upload was issued.
     * @remarks Must run on the render thread. The arguments are borrowed for this call only.
     * @ownership `world` and `graphics` remain owned by their callers and are never retained.
     * @lifetime Both arguments need only remain valid for the duration of this call.
     */
    int sync(pixelworld::PixelWorld* world, graphics::Graphics* graphics);

    /**
     * @brief Draw Chunk activity/temperature outlines and a bounded step-time graph.
     * @param world Borrowed authoritative world for this call only.
     * @param graphics Borrowed render-thread graphics service for this call only.
     * @param scale Screen pixels per simulated cell; must be positive.
     * @return Number of allocated Chunk diagnostics drawn.
     */
    int drawDiagnostics(pixelworld::PixelWorld* world, graphics::Graphics* graphics,
                        float scale);

    /**
     * @brief Borrow the stable backend-owned atlas texture, or null before first sync.
     * @ownership Graphics owns the returned pointer; callers must not delete it.
     * @lifetime Valid until the originating Graphics releases textures or is destroyed.
     */
    graphics::Texture* texture() const noexcept;
    int originX() const noexcept;
    int originY() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    std::uint64_t renderedRevision() const noexcept;
    std::uint64_t totalUploadedChunks() const noexcept;
    std::uint64_t uploadCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Optional rendering satellite for the backend-neutral PixelWorld module. */
class PixelWorldGraphics : public Module {
public:
    Module_REG(PixelWorldGraphics);

    /**
     * @brief Create a caller-owned atlas renderer for one world-space rectangle.
     * @return Newly allocated non-null renderer owned by the caller.
     * @ownership The caller must delete the returned renderer.
     * @lifetime If synchronized, the renderer must not be used after its Graphics backend.
     */
    [[nodiscard("retain and delete the returned PixelWorldAtlasRenderer")]]
    PixelWorldAtlasRenderer* newRenderer(int originX, int originY, int width, int height);
};

}  // namespace eve::pixelworld_graphics
