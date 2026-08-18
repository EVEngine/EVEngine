#pragma once

#include "common/ECS.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"

#include <functional>
#include <string>

namespace eve::graphics {

class Graphics;

class Camera3D : public ecs::Entity {
public:
    ENTITY(Camera3D, ecs::Entity)

    void release() override {}

    struct Data {
        float eyeX = 0.f, eyeY = 0.f, eyeZ = 3.f;
        float targetX = 0.f, targetY = 0.f, targetZ = 0.f;
        float upX = 0.f, upY = 1.f, upZ = 0.f;
        float fovYDeg = 60.f, nearZ = 0.1f, farZ = 100.f;
        float ambientR = 0.12f, ambientG = 0.12f, ambientB = 0.14f;
        Texture *envMap = nullptr;   // cubemap; nullptr → no IBL
        float envIntensity = 1.f;
        bool active = false;
        Camera3D *entity = nullptr;
        // Last screenToRay() result (origin = eye, dir normalized).
        float screenRayOx = 0.f, screenRayOy = 0.f, screenRayOz = 0.f;
        float screenRayDx = 0.f, screenRayDy = 0.f, screenRayDz = -1.f;
    };

    COMPONENT(Data, data)

    static Camera3D *createCamera() {
        Camera3D *c = Camera3D::create();
        ASSERT(c != nullptr);
        c->data()->entity = c;
        c->data()->active = true;
        return c;
    }

    void setEye(float x, float y, float z);
    void setTarget(float x, float y, float z);
    void setUp(float x, float y, float z);
    void setFov(float fovYDeg);
    void setActive(bool active);
    void setAmbient(float r, float g, float b);
    /** @brief Specular IBL cubemap (Graphics::newCubemap). nullptr disables IBL. */
    void setEnvMap(Texture *cube);
    void setEnvIntensity(float intensity);

    /**
     * @brief Build a world-space picking ray from a screen pixel.
     * Stores origin (camera eye) and normalized direction; read via getScreenRay*.
     * viewW/viewH must match the drawable used for rendering.
     */
    void  screenToRay(float screenX, float screenY, float viewW, float viewH);
    float getScreenRayOriginX();
    float getScreenRayOriginY();
    float getScreenRayOriginZ();
    float getScreenRayDirX();
    float getScreenRayDirY();
    float getScreenRayDirZ();
};

class Renderable3D : public ecs::Entity {
public:
    ENTITY(Renderable3D, ecs::Entity)

    void release() override {}

    struct Transform3D {
        float x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0; // radians
        float sx = 1, sy = 1, sz = 1;
    };

    struct MeshRenderer {
        static constexpr int kMaxLodLevels = 4;
        static constexpr int kMaxParts = Material::kMaxPartsHint;

        Mesh *mesh = nullptr;
        Texture *texture = nullptr;
        Texture *normalTexture = nullptr;  // nullptr → flat normal in default PBR path
        Texture *heightTexture = nullptr;  // nullptr → no parallax height (R = height)
        Shader *shader = nullptr;          // nullptr → default mesh3d PBR pipeline
        /** @brief Optional packed material; when set, overrides texture/shader/PBR fields below. */
        Material *material = nullptr;
        /**
         * @brief Optional X-ray shader used to paint this entity's occluded silhouette
         * (see Shader::setXray). When xrayHighlight is set, the entity is skipped
         * from the G-buffer (so its pixels record occluder depth) and drawn a
         * second time over the scene with the X-ray shader. Only the part hidden
         * behind buildings shows the highlight; visible parts render normally.
         */
        Shader *xrayShader = nullptr;
        bool xrayHighlight = false;
        float r = 1, g = 1, b = 1, a = 1;
        float metallic = 0.f;
        float roughness = 0.45f;
        float texBombScale = 4.f;     // cells per UV unit
        float texBombStrength = 0.f;  // 0 = off (default, preserves tiling)
        float texBombRot = 1.f;       // 0..1 per-cell rotation amount
        float parallaxScale = 0.f;    // 0 = off (default)
        float parallaxMinLayers = 8.f;
        float parallaxMaxLayers = 32.f;
        bool visible = true;
        bool receiveLight = true;
        bool castShadow = true;
        bool receiveShadow = true;
        bool castOcclusion = true;  // volumetric occlusion (screen-space shafts)
        /** @brief Alpha-blended hair/fur card pass (drawn after opaque meshes, back-to-front). */
        bool isHair = false;
        Camera3D *camera = nullptr;

        /**
         * @brief Multi-part model slots (one mesh + material per Assimp mesh / body region).
         * When partCount > 0, each part is drawn; otherwise `mesh` + material/fields.
         */
        int partCount = 0;
        ModelPart parts[kMaxParts] = {};

        /**
         * @brief Optional geometric LOD. When lodCount > 0, lodMeshes[0..lodCount) are used
         * instead of `mesh` based on camera distance. lodDistances[i] is the distance
         * at which rendering switches from lodMeshes[i] to lodMeshes[i+1].
         */
        int lodCount = 0;
        Mesh *lodMeshes[kMaxLodLevels] = {};
        float lodDistances[kMaxLodLevels - 1] = {25.f, 60.f, 120.f};

        /** @brief Pick LOD mesh for a camera distance; falls back to `mesh` when LOD disabled. */
        Mesh *meshForDistance(float distance) const {
            if (lodCount <= 0) return mesh;
            int level = 0;
            while (level + 1 < lodCount && distance >= lodDistances[level]) ++level;
            Mesh *picked = lodMeshes[level];
            return picked ? picked : mesh;
        }

        int lodLevelForDistance(float distance) const {
            if (lodCount <= 0) return 0;
            int level = 0;
            while (level + 1 < lodCount && distance >= lodDistances[level]) ++level;
            return level;
        }

        bool usesParts() const { return partCount > 0; }

        bool effectiveHair() const {
            if (material) return material->isTransparentHair();
            return isHair;
        }

        bool effectiveCastShadow() const {
            if (material) return material->getCastShadow();
            return castShadow;
        }
    };

    COMPONENT(Transform3D, transform)
    COMPONENT(MeshRenderer, meshRenderer)

    void setPosition(float x, float y, float z);
    void setRotation(float yaw, float pitch, float roll);
    void setYaw(float yaw);
    float getYaw();
    void setScale(float sx, float sy, float sz);
    void setMesh(Mesh *mesh);
    void setTexture(Texture *texture);
    void setNormalTexture(Texture *texture);
    /** @brief Height map for parallax (R channel; white = raised). nullptr disables sampling. */
    void setHeightTexture(Texture *texture);
    void setShader(Shader *shader);
    /** @brief Attach a Material that packages shading method + surface params. */
    void setMaterial(Material *material);
    Material *getMaterial();
    /** @brief Attach an X-ray mesh shader (see Shader::setXray) for occluded silhouettes. */
    void setXRayShader(Shader *shader);
    Shader *getXRayShader();
    /**
     * @brief When true, this entity is an X-ray target: its G-buffer pixels are replaced
     * by occluders and it is redrawn with xrayShader so the part hidden behind
     * buildings shows as a highlight silhouette (visible parts stay normal).
     */
    void setXRayHighlight(bool on);
    bool getXRayHighlight();
    /**
     * @brief Bind a named mesh+material part (e.g. Assimp submesh / body region).
     * index 0..kMaxParts-1. Passing nullptr mesh clears that slot and trims partCount.
     */
    void setPart(int index, const std::string &name, Mesh *mesh, Material *material);
    void clearParts();
    int getPartCount();
    std::string getPartName(int index);
    Mesh *getPartMesh(int index);
    Material *getPartMaterial(int index);
    void setHair(bool hair);
    bool getHair();
    void setTint(float r, float g, float b, float a = 1.f);
    void setMetallic(float metallic);
    void setRoughness(float roughness);
    /**
     * @brief Texture cell bombing — random per-cell UV offset/rotation blended across a 2×2
     * neighborhood to hide tiling. strength 0 disables (default).
     */
    void setTexCellBomb(float cellScale, float strength, float rotAmount = 1.f);
    float getTexCellBombScale();
    float getTexCellBombStrength();
    float getTexCellBombRotation();
    /**
     * @brief Parallax occlusion mapping. scale 0 disables (default). Typical scale 0.02..0.08.
     * Requires a height texture via setHeightTexture.
     */
    void setParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f);
    float getParallaxScale();
    float getParallaxMinLayers();
    float getParallaxMaxLayers();
    void setVisible(bool visible);
    void setReceiveLight(bool receive);
    void setCastShadow(bool cast);
    void setReceiveShadow(bool receive);
    void setCastOcclusion(bool cast);
    bool getCastOcclusion();
    void setCamera(Camera3D *camera);

    /**
     * @brief Configure geometric LOD. index 0 = highest detail.
     * For index > 0, switchDistance is the camera distance that selects this level
     * (stored in lodDistances[index-1]). Passing nullptr mesh clears that slot.
     */
    void setMeshLod(int index, Mesh *mesh, float switchDistance = 0.f);
    void clearMeshLod();
    int getMeshLodCount();
    int getMeshLodLevelAtDistance(float distance);
};

class RenderSystem3D {
public:
    static void render(Graphics &gfx);

    /**
     * @brief Register a callback that fills the G-buffer (depth/normal/albedo) for
     * geometry outside the Renderable3D ECS (e.g. sprite-stack slices). Called
     * inside the GBuffer pass after opaque meshes, before endGBufferPass.
     * The camera data and view-projection match the pass camera.
     */
    using GBufferExtraDrawer =
        std::function<void(Graphics &gfx, const Camera3D::Data &cam,
                           const glm::mat4 &viewProj, float aspect)>;
    static void addGBufferExtraDrawer(GBufferExtraDrawer drawer);

    /**
     * @brief Register a callback that casts shadows for geometry outside the
     * Renderable3D ECS (e.g. sprite-stack slices). Invoked inside each CSM
     * cascade pass with that cascade's light-view-projection; call
     * gfx.drawMeshShadowAlpha(...) / drawMeshShadow(...) there. When no Light3D
     * casts shadows but drawers are registered, the legacy directional light is
     * used as the shadow source.
     */
    using ShadowExtraDrawer =
        std::function<void(Graphics &gfx, const glm::mat4 &lightVP,
                           const Camera3D::Data &cam)>;
    static void addShadowExtraDrawer(ShadowExtraDrawer drawer);

    /** @brief Legacy single directional light used when no enabled Light3D exists. */
    static void setDirectionalLight(float dx, float dy, float dz, float r, float g, float b);
};

} // namespace eve::graphics
