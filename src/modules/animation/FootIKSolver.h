#pragma once

namespace eve::animation {
class AnimPose;
class AnimSkeleton;

/** @brief Result of a Foot IK ground query. */
enum class FootIKGroundQueryStatus { Unavailable, NoHit, Hit };

/** @brief Physics-neutral downward ground-query provider for Foot IK. */
class FootIKGroundProvider {
public:
    virtual ~FootIKGroundProvider() = default;
    /** @brief Query ground below an origin and return model-space contact data. */
    virtual FootIKGroundQueryStatus queryGround(float originX, float originY, float originZ, float maxDistance,
                                                 float& hitX, float& hitY, float& hitZ, float& normalX,
                                                 float& normalY, float& normalZ) const = 0;
};

/** @brief Immutable model-space state for one foot in a tooling snapshot. */
struct FootIKDebugFoot { bool configured=false,contact=false,locked=false,toeConfigured=false; float x=0.f,y=0.f,z=0.f,nx=0.f,ny=1.f,nz=0.f,toeX=0.f,toeY=0.f,toeZ=0.f; };
/** @brief Owning renderer-neutral paired-foot tooling snapshot. */
struct FootIKDebugSnapshot { FootIKDebugFoot left,right; };

/**
 * @brief Paired-foot IK with contact smoothing and pelvis compensation.
 * @note Main-thread only. The borrowed skeleton must outlive this object.
 */
class FootIKSolver {
public:
    /** @brief Construct for a non-null borrowed skeleton. */
    explicit FootIKSolver(AnimSkeleton* skeleton);
    ~FootIKSolver();
    FootIKSolver(const FootIKSolver&) = default;
    FootIKSolver& operator=(const FootIKSolver&) = default;
    /** @brief Rebind; changing skeleton clears all bone-index configuration. */
    void setSkeleton(AnimSkeleton* skeleton);
    /**
     * @brief Return the borrowed skeleton, or null.
     * @ownership Borrowed; ownership remains with the animation source.
     * @lifetime Valid until setSkeleton(), skeleton destruction, or solver destruction.
     */
    AnimSkeleton* getSkeleton() const { return skeleton_; }
    /** @brief Configure the pelvis bone. */
    void setPelvisBone(int bone);
    /** @brief Configure the left hip-knee-foot chain and sole offset. */
    void configureLeftLeg(int hip, int knee, int foot, float soleOffset = 0.f);
    /** @brief Configure the right hip-knee-foot chain and sole offset. */
    void configureRightLeg(int hip, int knee, int foot, float soleOffset = 0.f);
    /** @brief Configure an optional left toe child and its sole offset. */
    void configureLeftToe(int toe, float soleOffset = 0.f);
    /** @brief Configure an optional right toe child and its sole offset. */
    void configureRightToe(int toe, float soleOffset = 0.f);
    /**
     * @brief Set an optional automatic ground-query provider.
     * @ownership Borrowed; ownership remains with the caller.
     * @lifetime The provider must outlive this solver or be cleared before destruction.
     */
    void setGroundProvider(const FootIKGroundProvider* provider) { groundProvider_ = provider; }
    /**
     * @brief Return the borrowed ground provider, or null.
     * @ownership Borrowed; ownership remains with the caller.
     * @lifetime Valid until setGroundProvider(), provider destruction, or solver destruction.
     */
    const FootIKGroundProvider* getGroundProvider() const { return groundProvider_; }
    /** @brief Set ray origin height and downward query distance. */
    void setGroundQuery(float startHeight, float distance);
    /** @brief Supply the left model-space ground contact. */
    void setLeftContact(bool hit, float x, float y, float z, float nx, float ny, float nz,
                        float weight = 1.f);
    /** @brief Supply the right model-space ground contact. */
    void setRightContact(bool hit, float x, float y, float z, float nx, float ny, float nz,
                         float weight = 1.f);
    /** @brief Set maximum pelvis compensation distance. */
    void setMaxPelvisOffset(float distance);
    /** @brief Set minimum accepted upward normal cosine in [0, 1]. */
    void setMinGroundNormalY(float value);
    /** @brief Set contact interpolation response in inverse seconds. */
    void setPositionResponse(float value);
    /** @brief Set foot-normal interpolation response in inverse seconds. */
    void setRotationResponse(float value);
    /** @brief Set how long a missing automatic contact retains its prior target. */
    void setContactGraceTime(float seconds);
    /** @brief Enable or disable world-space foot locking. */
    void setFootLockEnabled(bool enabled);
    /** @brief Set lock enter and release contact-weight thresholds. */
    void setFootLockThresholds(float enterWeight, float exitWeight);
    /** @brief Return whether the left foot is currently locked. */
    bool isLeftFootLocked() const { return left_.locked; }
    /** @brief Return whether the right foot is currently locked. */
    bool isRightFootLocked() const { return right_.locked; }
    /** @brief Clear interpolated contacts. */
    void reset();
    /** @brief Apply using injected delta time and owning contact values. */
    void apply(AnimPose* pose, float dt);
    /** @brief Copy smoothed contacts and lock state for tooling visualization. */
    FootIKDebugSnapshot debugSnapshot() const;

private:
    struct Contact { float x=0.f,y=0.f,z=0.f,nx=0.f,ny=1.f,nz=0.f,weight=0.f; };
    struct Leg {
        int hip=-1,knee=-1,foot=-1,toe=-1;
        float soleOffset=0.f,toeSoleOffset=0.f,missingTime=0.f;
        Contact target,smooth,toeTarget,toeSmooth,lockedContact;
        bool configured=false,toeConfigured=false,initialized=false,toeInitialized=false,locked=false;
    };
    void configure(Leg& leg,int hip,int knee,int foot,float soleOffset);
    void contact(Leg& leg,bool hit,float x,float y,float z,float nx,float ny,float nz,float weight);
    void configureToe(Leg& leg,int toe,float soleOffset);
    void queryGround(Leg& leg,const AnimPose& pose,float dt);
    void updateLock(Leg& leg);
    void interpolate(Leg& leg,float dt);
    float pelvisOffset(const Leg& leg,const AnimPose& pose) const;
    void solve(const Leg& leg,AnimPose& pose,float rotationWeight) const;

    AnimSkeleton* skeleton_=nullptr;
    int pelvisBone_=-1;
    Leg left_,right_;
    float maxPelvisOffset_=0.35f,minGroundNormalY_=0.35f;
    float positionResponse_=18.f,rotationResponse_=14.f;
    const FootIKGroundProvider* groundProvider_=nullptr;
    float groundStartHeight_=0.5f,groundQueryDistance_=1.5f,contactGraceTime_=0.08f;
    float lockEnterWeight_=0.8f,lockExitWeight_=0.2f;
    bool footLockEnabled_=false;
};
}  // namespace eve::animation
