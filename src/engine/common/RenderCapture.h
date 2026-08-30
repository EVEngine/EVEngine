#pragma once

// Capability interface: render-frame capture and scene inspection for
// devtools / MCP. Provided by the graphics module (which may reach into scene
// for the entity-id mask and visible-entity JSON).

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <string>

namespace eve {

/** @brief Render surface / frame state snapshot (value type). */
struct EVENGINE_API RenderStatusInfo {
    int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
    bool had3DThisFrame = false;
    bool readbackEnabled = false;
    std::string backend;
};

/** @brief Generation-qualified snapshot of one live field-backed Renderable3D. */
struct EVENGINE_API Renderable3DInfo {
    std::uint32_t entityId = 0;
    std::uint32_t generation = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    float tintR = 1.f, tintG = 1.f, tintB = 1.f, tintA = 1.f;
    float metallic = 0.f, roughness = 0.45f, parallaxScale = 0.f;
    bool visible = true, receiveLight = true, castShadow = true, receiveShadow = true;
    bool hasPackedMaterial = false, hasTexture = false, hasShader = false;
};

/** @brief Frame capture + camera + visible-entity inspection (graphics). */
class EVENGINE_API IRenderCapture {
public:
    static constexpr const char* capabilityName = "IRenderCapture";

    virtual ~IRenderCapture() = default;

    virtual RenderStatusInfo status() const = 0;
    /** @brief Enable/disable full-frame readback for subsequent presents. */
    virtual void setReadbackEnabled(bool enabled) = 0;
    /** @brief Capture the last presented frame and write a PNG. */
    virtual bool savePng(const std::string& path, int* outWidth, int* outHeight,
                         std::string* err) = 0;
    /** @brief Capture the last presented frame as a base64 PNG data URL. */
    virtual std::string capturePngDataUrl() = 0;

    /** @brief Find the active Camera3D (create one lazily); true when usable. */
    virtual bool ensureCamera() = 0;
    /** @brief Set camera eye/target/fov (fov <= 0 keeps current). */
    virtual bool setCameraPose(float ex, float ey, float ez, float tx, float ty, float tz,
                               float fovYDeg) = 0;
    /** @brief Set camera eye + yaw/pitch (degrees) + fov. */
    virtual bool setCameraPoseYawPitch(float ex, float ey, float ez, float yawDeg,
                                       float pitchDeg, float fovYDeg) = 0;
    /** @brief Current camera pose as JSON (eye/target/fov/viewport). */
    virtual std::string cameraPoseJson() = 0;

    /**
     * @brief Inspect one live Renderable3D using its complete ECS identity.
     * @return Owning value snapshot, or a structured stale-handle failure.
     * @thread Engine-main-thread only.
     * @reentrancy Does not invoke scripts or callbacks.
     */
    [[nodiscard]] virtual Result<Renderable3DInfo> inspectRenderable3D(
        std::uint32_t entityId, std::uint32_t generation) = 0;

    /** @brief Visible-entity 3D snapshot JSON for the current camera. */
    virtual std::string visibleEntitiesJson(float fovYDeg, bool* ok) = 0;
    /** @brief Visible-entity 3D snapshot JSON at an explicit camera pose. */
    virtual std::string visibleEntitiesJsonAt(float ex, float ey, float ez, float tx, float ty,
                                              float tz, float fovYDeg, bool* ok) = 0;
    /** @brief Per-pixel entity-ID mask JSON (id -> node map). */
    virtual std::string entityIdMaskJson(int* outWidth, int* outHeight, bool* ok) = 0;
    /** @brief Render the entity-ID mask and write it as a PNG. */
    virtual bool entityIdMaskPng(const std::string& path, std::string* err) = 0;
    /** @brief Save a G-buffer attachment ("depth"|"normal") as PNG. */
    virtual bool gbufferPng(const std::string& name, const std::string& path,
                            std::string* err) = 0;
};

}  // namespace eve
