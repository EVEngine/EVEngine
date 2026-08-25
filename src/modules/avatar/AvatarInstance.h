#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
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

namespace eve::scene {
class Scene;
}

namespace eve::animation {
class AnimClip;
class AnimPlayer;
class AnimLayerMixer;
class AnimPose;
class AnimSkin;
class AnimSkeleton;
class AnimStateMachine;
class Tween;
}

namespace eve::avatar {

/** @brief Optional Live2D runtime backend (Cubism etc.). Registered from C++ / plugins. */
class ILive2DBackend {
public:
    virtual ~ILive2DBackend() = default;
    virtual std::string getName() const = 0;
    /** @brief True only for a backend that can render a real Live2D model. */
    virtual bool  isRuntimeAvailable() const { return true; }
    virtual bool loadModel(const std::string &path) = 0;
    virtual void update(float dt) = 0;
    virtual void setParameter(const std::string &name, float value) = 0;
    virtual float getParameter(const std::string &name) const = 0;
    virtual bool setExpression(const std::string &name) = 0;
    virtual bool setMotion(const std::string &name) = 0;
    /** @brief Optional: receive the Avatar's 2D transform. */
    virtual void setTransform(float /*x*/, float /*y*/, float /*sx*/, float /*sy*/) {}
    /** @brief Optional: receive the Avatar's effective visibility. */
    virtual void setVisible(bool /*visible*/) {}
    /** @brief Optional: receive the Avatar's render sorting layer. */
    virtual void setLayer(int /*layer*/) {}
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
    /** @brief Callback fired exactly once, when the instance is destroyed. */
    using DestroyHook = std::function<void(AvatarInstance *)>;

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
    /** @brief Define reflected Avatar parameter metadata for project-built inspectors. @return False for an empty name or invalid range. */
    bool defineParameter(const std::string& name, float defaultValue, float minimum, float maximum);
    /** @brief Return a reflected parameter default, or zero for an unknown parameter. */
    float getParameterDefault(const std::string& name) const;
    /** @brief Return a reflected parameter minimum, or zero for an unknown parameter. */
    float getParameterMinimum(const std::string& name) const;
    /** @brief Return a reflected parameter maximum, or one for an unknown parameter. */
    float getParameterMaximum(const std::string& name) const;

    void update(float dt);
    /** @brief Push image / live2d layers into ECS or draw queues. */
    void sync();

    void release();

    /** @brief Register a destruction hook; returns an id usable with removeDestroyHook(). */
    size_t addDestroyHook(DestroyHook hook);
    /** @brief Remove a previously registered destruction hook. */
    void removeDestroyHook(size_t id);

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
    /** @brief Return the native Renderable2D owned by a named image layer. */
    graphics::Renderable2D* getLayerRenderable(const std::string& name);
    /** @brief Spec: "eyes=1;mouth=smile;blush=0" (semicolon-separated name=value). */
    bool defineExpression(const std::string &name, const std::string &spec);
    /** @brief Remove a project-defined expression. */
    bool removeExpression(const std::string& name);
    /** @brief Return the number of project-defined expressions. */
    int getExpressionCount() const;
    /** @brief Return a stable sorted expression name, or empty text. */
    std::string getExpressionName(int index) const;
    bool applyExpression(const std::string &name);
    /** @brief Blend numeric/bool expression channels over duration seconds. */
    bool transitionExpression(const std::string& name, float duration);

    // ---- live2d kind ----
    bool loadLive2DModel(const std::string &path);
    std::string getLive2DBackendName() const;
    bool hasLive2DBackend() const;
    /** @brief Append this instance's Live2D drawables to a shared draw queue. */
    void collectLive2DDrawItems(std::vector<graphics::DrawItem2D>& out);

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

    // ---- skeletal animation ----
    /** @brief Bind a clip player that the avatar advances and skins each update. */
    bool bindAnimPlayer(animation::AnimPlayer* player);
    /** @brief Bind an animation state machine that the avatar advances each update. */
    bool bindAnimStateMachine(animation::AnimStateMachine* machine);
    /** @brief Bind an override/additive layer mixer that the avatar advances each update. */
    bool bindAnimLayerMixer(animation::AnimLayerMixer* mixer);
    /** @brief Bind CPU skin data used to deform the avatar mesh from the active pose. */
    bool bindAnimSkin(animation::AnimSkin* skin);
    /** @brief Register a motion name for setMotion(); the clip is not owned. */
    bool registerMotion(const std::string& name, animation::AnimClip* clip);
    /** @brief Remove a registered motion without destroying its clip. */
    bool unregisterMotion(const std::string& name);
    /** @brief Return the number of registered skeletal motions. */
    int getMotionCount() const;
    /** @brief Return a stable sorted motion name, or empty text. */
    std::string getMotionName(int index) const;
    /** @brief Return a registered non-owned motion clip, or nullptr. */
    animation::AnimClip* getMotionClip(const std::string& name) const;
    /** @brief Set the cross-fade duration used when switching registered motions. */
    void setMotionBlendTime(float seconds);
    /** @brief Apply root-bone X/Z deltas to the Avatar transform while animating. */
    void setApplyRootMotion(bool enabled);
    /** @brief Return whether root motion is applied to the Avatar transform. */
    bool getApplyRootMotion() const { return applyRootMotion_; }
    /** @brief Last evaluated root-motion X delta. */
    float getRootMotionDeltaX() const { return rootMotionDeltaX_; }
    /** @brief Last evaluated root-motion Z delta. */
    float getRootMotionDeltaZ() const { return rootMotionDeltaZ_; }
    /** @brief Number of animation events emitted by the latest update. */
    int getAnimationEventCount() const;
    /** @brief Source layer for an animation event ("base" for the base player). */
    std::string getAnimationEventLayer(int index) const;
    /** @brief Animation event name, or empty for an invalid index. */
    std::string getAnimationEventName(int index) const;
    /** @brief Animation event payload, or empty for an invalid index. */
    std::string getAnimationEventPayload(int index) const;
    /** @brief Map a VRM humanoid semantic (for example "head") to a skeleton bone. */
    bool mapHumanoidBone(const std::string& semantic, const std::string& boneName);
    /** @brief Auto-map common VRM humanoid semantics from the currently bound skeleton. */
    int autoMapHumanoidBones();
    /** @brief Return the mapped skeleton bone name, or an empty string. */
    std::string getHumanoidBoneName(const std::string& semantic) const;
    /** @brief Map a VRM viseme/expression semantic to an existing mesh morph. */
    bool mapViseme(const std::string& viseme, const std::string& morphName);
    /** @brief Set a mapped viseme weight and clear the previously active viseme. */
    bool setViseme(const std::string& viseme, float weight);
    /** @brief Set a world-space target tracked by the mapped humanoid head bone. */
    bool setLookAtTarget(float x, float y, float z);
    /** @brief Set VRM-style head look-at influence in the range 0..1. */
    void setLookAtWeight(float weight);
    /** @brief Disable procedural head look-at. */
    void clearLookAtTarget();
    /**
     * @brief Attach a non-owned Renderable3D to a mapped humanoid semantic or bone name.
     * @param name
     * Stable attachment name used by detachAttachment().
     * @param boneSemanticOrName Humanoid semantic (for
     * example "rightHand") or bone name.
     * @param renderable Existing renderable; Avatar does not own or destroy
     * it.
     * @param ox Local bone-space X offset.
     * @param oy Local bone-space Y offset.
     * @param oz
     * Local bone-space Z offset.
     */
    bool attachToBone(const std::string& name, const std::string& boneSemanticOrName,
                      graphics::Renderable3D* renderable, float ox = 0.f, float oy = 0.f, float oz = 0.f);
    /** @brief Remove an attachment without destroying its Renderable3D. */
    bool detachAttachment(const std::string& name);
    /** @brief Return the number of active bone attachments. */
    int getAttachmentCount() const { return static_cast<int>(attachments_.size()); }
    /** @brief Link this avatar to a node in Scene's current host. */
    bool linkSceneNode(scene::Scene* scene, const std::string& nodeId);
    /** @brief Return whether this avatar is currently scene-driven. */
    bool isSceneLinked() const { return sceneLinked_; }

    // ---- animation tween binding ----
    /** @brief Drive x/y/sx/sy and matching parameters from a Tween each update(). */
    void bindTween(animation::Tween *tween);
    void unbindTween();
    animation::Tween *getBoundTween() const { return tween_; }

private:
    struct Layer {
        std::string name;
        graphics::Renderable2D *entity = nullptr;
        int zIndex = 0;
        bool visible = true;
        bool                    autoSize = true;
        float ox = 0.f, oy = 0.f;
        float                   appliedParentRotation = 0.f;
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
    struct ParameterMetadata {
        float defaultValue = 0.f;
        float minimum      = 0.f;
        float maximum      = 1.f;
    };
    std::unordered_map<std::string, ParameterMetadata> parameterMetadata_;

    // image
    std::vector<Layer> layers_;
    std::unordered_map<std::string, std::string> expressionDefs_;
    std::unordered_map<std::string, float>       expressionBlendFrom_;
    std::unordered_map<std::string, float>       expressionBlendTo_;
    float                                        expressionBlendElapsed_  = 0.f;
    float                                        expressionBlendDuration_ = 0.f;
    std::vector<std::pair<size_t, DestroyHook>> destroyHooks_;
    size_t nextDestroyHookId_ = 0;

    // live2d
    ILive2DBackend *live2d_ = nullptr;

    // vroid
    std::string vroidPath_;
    model3d::ModelData *vroidData_ = nullptr;
    graphics::Renderable3D *renderable3d_ = nullptr;
    graphics::Renderable2D* sceneAnchor2d_ = nullptr;
    graphics::Mesh *boundMesh_ = nullptr;
    float x3_ = 0.f, y3_ = 0.f, z3_ = 0.f;
    float yaw_ = 0.f, pitch_ = 0.f, roll_ = 0.f;
    float sx3_ = 1.f, sy3_ = 1.f, sz3_ = 1.f;

    animation::Tween *tween_ = nullptr;
    animation::AnimPlayer*                                animPlayer_       = nullptr;
    animation::AnimLayerMixer*                            animLayerMixer_   = nullptr;
    animation::AnimStateMachine*                          animStateMachine_ = nullptr;
    animation::AnimSkin*                                  animSkin_         = nullptr;
    animation::AnimSkeleton*                              animSkeleton_     = nullptr;
    std::unordered_map<std::string, animation::AnimClip*> motions_;
    std::unordered_map<std::string, std::string>          humanoidBones_;
    std::unordered_map<std::string, std::string>          visemeMorphs_;
    std::string                                           activeViseme_;
    struct Attachment {
        std::string             name;
        std::string             boneName;
        graphics::Renderable3D* renderable = nullptr;
        float                   ox = 0.f, oy = 0.f, oz = 0.f;
    };
    std::vector<Attachment> attachments_;
    float                   lookAtX_ = 0.f, lookAtY_ = 0.f, lookAtZ_ = 1.f;
    float                   lookAtWeight_  = 1.f;
    bool                    lookAtEnabled_ = false;
    bool                    lookAtApplied_ = false;
    float                   lookAtBaseQx_ = 0.f, lookAtBaseQy_ = 0.f, lookAtBaseQz_ = 0.f, lookAtBaseQw_ = 1.f;
    float                   lookAtPostQx_ = 0.f, lookAtPostQy_ = 0.f, lookAtPostQz_ = 0.f, lookAtPostQw_ = 1.f;
    std::vector<float>      skinnedPositions_;
    float                   motionBlendTime_  = 0.2f;
    float                   rootMotionDeltaX_ = 0.f, rootMotionDeltaZ_ = 0.f;
    float                   previousRootX_ = 0.f, previousRootZ_ = 0.f;
    bool                    applyRootMotion_     = false;
    bool                    hasPreviousRoot_     = false;
    int                     rootMotionLoopCount_ = 0;
    bool                    sceneLinked_         = false;

    void ensureParameter(const std::string &name, float value = 0.f);
    void applyTweenTracks();
    void syncMorphWeightsToMesh();
    animation::AnimPose* updateSkeletalAnimation(float dt);
    void                 updateRootMotion(animation::AnimPose* pose);
    void                 updateSkin(animation::AnimPose* pose);
    void                 updateAttachments(animation::AnimPose* pose);
    void                 applyLookAt(animation::AnimPose* pose);
    void                 updateExpressionTransition(float dt);

    bool released_ = false;
};

}  // namespace eve::avatar
