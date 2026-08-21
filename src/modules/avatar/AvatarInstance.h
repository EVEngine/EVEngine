#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
struct DrawItem2D;
class Mesh;
class Renderable2D;
class Renderable3D;
class Texture;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}

namespace eve::model3d {
class ModelData;
}

namespace eve::animation {
class Tween;
}

namespace eve::avatar {

/** @brief Optional Live2D runtime backend (Cubism etc.). Registered from C++ / plugins. */
class ILive2DBackend {
public:
    virtual ~ILive2DBackend() = default;
    virtual std::string getName() const = 0;
    virtual bool loadModel(const std::string &path) = 0;
    virtual void update(float dt) = 0;
    virtual void setParameter(const std::string &name, float value) = 0;
    virtual float getParameter(const std::string &name) const = 0;
    virtual bool setExpression(const std::string &name) = 0;
    virtual bool setMotion(const std::string &name) = 0;
    /** @brief Optional: push drawables into the 2D queue. Default no-op. */
    virtual void collectDrawItems(std::vector<graphics::DrawItem2D> & /*out*/) {}
};

using Live2DBackendFactory = ILive2DBackend *(*)();

/**
 * @brief Unified avatar instance. Kind is a string: "image" | "live2d" | "vroid".
 * Script-facing API avoids overloads; kind-specific methods no-op / return false
 * when unsupported.
 */
class AvatarInstance {
public:
    explicit AvatarInstance(std::string kind);
    ~AvatarInstance();

    AvatarInstance(const AvatarInstance &) = delete;
    AvatarInstance &operator=(const AvatarInstance &) = delete;

    std::string getKind() const { return kind_; }

    void setPosition(float x, float y);
    float getX() const { return x_; }
    float getY() const { return y_; }

    void setScale(float sx, float sy);
    float getScaleX() const { return sx_; }
    float getScaleY() const { return sy_; }

    void setVisible(bool visible);
    bool isVisible() const { return visible_; }

    void setLayer(int layer);
    int getLayer() const { return layer_; }

    void setExpression(const std::string &name);
    std::string getExpression() const { return expression_; }

    void setMotion(const std::string &name);
    std::string getMotion() const { return motion_; }

    void setParameter(const std::string &name, float value);
    float getParameter(const std::string &name) const;
    bool hasParameter(const std::string &name) const;
    int getParameterCount() const;
    std::string getParameterName(int index) const;

    void update(float dt);
    /** @brief Push image / live2d layers into ECS or draw queues. */
    void sync();

    void release();

    // ---- image kind ----
    bool addLayer(const std::string &name, graphics::Texture *texture, int zIndex);
    bool setLayerTexture(const std::string &name, graphics::Texture *texture);
    bool setLayerVisible(const std::string &name, bool visible);
    bool setLayerOffset(const std::string &name, float ox, float oy);
    bool setLayerColor(const std::string &name, float r, float g, float b, float a);
    bool setLayerZ(const std::string &name, int zIndex);
    bool setLayerSize(const std::string &name, float w, float h);
    int getLayerCount() const;
    std::string getLayerName(int index) const;
    bool hasLayer(const std::string &name) const;
    /** @brief Spec: "eyes=1;mouth=smile;blush=0" (semicolon-separated name=value). */
    bool defineExpression(const std::string &name, const std::string &spec);
    bool applyExpression(const std::string &name);

    // ---- live2d kind ----
    bool loadLive2DModel(const std::string &path);
    std::string getLive2DBackendName() const;
    bool hasLive2DBackend() const;

    // ---- vroid kind ----
    bool loadVroidModelPath(const std::string &path);
    bool bindVroidModelData(model3d::ModelData *data);
    /** @brief Register morph target names from ModelData as parameters (weights default 0). */
    int loadMorphNamesFromModel(int meshIndex = 0);
    void setMesh(graphics::Mesh *mesh);
    void setTexture(graphics::Texture *texture);
    void setPosition3D(float x, float y, float z);
    void setRotation3D(float yaw, float pitch, float roll);
    void setScale3D(float sx, float sy, float sz);
    graphics::Renderable3D *getRenderable3D() const { return renderable3d_; }
    graphics::Mesh *getBoundMesh() const;
    std::string getVroidModelPath() const { return vroidPath_; }
    /** @brief Push parameter weights onto Mesh morphs and bake GPU verts when possible. */
    bool bakeMorphs();

    // ---- animation tween binding ----
    /** @brief Drive x/y/sx/sy and matching parameters from a Tween each update(). */
    void bindTween(animation::Tween *tween);
    void unbindTween();
    animation::Tween *getBoundTween() const { return tween_; }

private:
    struct Layer {
        std::string name;
        graphics::Texture *texture = nullptr;
        graphics::Renderable2D *entity = nullptr;
        int zIndex = 0;
        bool visible = true;
        float ox = 0.f, oy = 0.f;
        float w = 0.f, h = 0.f;  // 0 → use texture size
        float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
    };

    Layer *findLayer(const std::string &name);
    const Layer *findLayer(const std::string &name) const;
    void syncImageLayers();
    void syncVroid();
    void destroyLayers();
    void destroyVroid();
    bool applyExpressionSpec(const std::string &spec);

    std::string kind_;
    float x_ = 0.f, y_ = 0.f;
    float sx_ = 1.f, sy_ = 1.f;
    bool visible_ = true;
    int layer_ = 100;
    std::string expression_;
    std::string motion_;
    std::unordered_map<std::string, float> parameters_;
    std::vector<std::string> parameterOrder_;

    // image
    std::vector<Layer> layers_;
    std::unordered_map<std::string, std::string> expressionDefs_;

    // live2d
    ILive2DBackend *live2d_ = nullptr;

    // vroid
    std::string vroidPath_;
    model3d::ModelData *vroidData_ = nullptr;
    graphics::Renderable3D *renderable3d_ = nullptr;
    graphics::Mesh *boundMesh_ = nullptr;
    float x3_ = 0.f, y3_ = 0.f, z3_ = 0.f;
    float yaw_ = 0.f, pitch_ = 0.f, roll_ = 0.f;
    float sx3_ = 1.f, sy3_ = 1.f, sz3_ = 1.f;

    animation::Tween *tween_ = nullptr;

    void ensureParameter(const std::string &name, float value = 0.f);
    void applyTweenTracks();
    void syncMorphWeightsToMesh();

    bool released_ = false;
};

}  // namespace eve::avatar
