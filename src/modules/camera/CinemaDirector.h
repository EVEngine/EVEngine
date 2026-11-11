#pragma once

#include "camera/CameraController.h"
#include "common/Export.h"
#include "common/Result.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ssq {
class Table;
}

namespace eve::camera {

/**
 * @brief Story-scene cinema director that push/pops overview shots and film moves on a CameraController.
 *
 * Gameplay framing stays owned by CameraController. While the stack is non-empty the director pauses
 * the priority-rig director, drives cinematic views, and restores the captured framing on the final pop.
 */
class EVENGINE_API_WORLD CinemaDirector {
public:
    /**
     * @brief Bind the gameplay camera controller this director will temporarily override.
     * @ownership Borrowed; the caller retains ownership of the controller.
     * @lifetime Must outlive this director while the story stack is non-empty.
     */
    [[nodiscard]] Result<void> bind(CameraController* controller);
    /** @brief Register or replace a named establishing / overview shot. */
    [[nodiscard]] Result<void> defineOverview(const std::string& name, float eyeX, float eyeY, float eyeZ,
                                              float targetX, float targetY, float targetZ, float fov);
    /**
     * @brief Push an overview shot onto the story stack, blending from the live pose.
     * @cost One framing capture (first push) plus a cinematic view switch.
     */
    [[nodiscard]] Result<void> pushOverview(const std::string& name, float blendTime);
    /**
     * @brief Dolly the eye along the look axis (positive toward subject).
     * @cost Constant setup; amortized across update frames for duration seconds.
     */
    [[nodiscard]] Result<void> dolly(float distance, float duration);
    /**
     * @brief Raise or lower the eye while keeping the look target fixed.
     * @cost Constant setup; amortized across update frames for duration seconds.
     */
    [[nodiscard]] Result<void> crane(float deltaY, float duration);
    /**
     * @brief Orbit the eye around the current look target by a finite yaw arc.
     * @cost Constant setup; amortized across update frames for duration seconds.
     */
    [[nodiscard]] Result<void> orbitReveal(float degrees, float duration);
    /**
     * @brief Pop the top story shot and restore the previous overview or gameplay framing.
     * @cost One framing restore or cinematic switch.
     */
    [[nodiscard]] Result<void> pop(float blendTime);
    /**
     * @brief Advance the active film move; call before CameraController::update.
     * @cost Constant per frame while a move is playing.
     */
    [[nodiscard]] Result<void> update(float dt);
    /** @brief True while at least one story shot is on the stack. */
    [[nodiscard]] bool isActive() const noexcept { return !stack_.empty(); }
    /** @brief Number of stacked story shots. */
    [[nodiscard]] int depth() const noexcept { return static_cast<int>(stack_.size()); }
    /** @brief True while a dolly/crane/orbitReveal is in progress. */
    [[nodiscard]] bool isMovePlaying() const noexcept { return move_.active; }
    /** @brief Name of the top overview, or empty when inactive. */
    [[nodiscard]] std::string getActiveOverview() const;

private:
    struct OverviewShot {
        std::string name;
        glm::vec3   eye{0.f};
        glm::vec3   target{0.f};
        float       fov = 48.f;
    };

    struct StackEntry {
        std::string           overview;
        CameraFramingSnapshot gameplay{};
        bool                  ownsGameplay = false;
    };

    enum class MoveKind { None, Dolly, Crane, OrbitReveal };

    struct MoveState {
        bool     active = false;
        MoveKind kind = MoveKind::None;
        float    age = 0.f;
        float    duration = 0.f;
        float    param = 0.f;
        glm::vec3 fromEye{0.f};
        glm::vec3 fromTarget{0.f};
        float     fromFov = 60.f;
        glm::vec3 toEye{0.f};
        glm::vec3 toTarget{0.f};
        float     toFov = 60.f;
        glm::vec3 orbitCenter{0.f};
        float     orbitRadius = 1.f;
        float     orbitElevation = 0.f;
        float     orbitAzimuth0 = 0.f;
    };

    [[nodiscard]] Result<void> requireBound() const;
    /**
     * @brief Look up a registered overview by name.
     * @ownership Borrowed pointer into this director's overview table; do not free.
     * @lifetime Valid until the next defineOverview that replaces that name, or director destruction.
     */
    [[nodiscard]] const OverviewShot* findOverview(const std::string& name) const;
    [[nodiscard]] Result<void> applyOverviewPose(const OverviewShot& shot, float blendTime);
    [[nodiscard]] Result<void> beginMove(MoveKind kind, float duration, float param);
    void cancelMove();
    [[nodiscard]] Result<void> sampleLivePose(glm::vec3& eye, glm::vec3& target, float& fov) const;
    [[nodiscard]] Result<void> writeActivePose(const glm::vec3& eye, const glm::vec3& target, float fov);

    CameraController*         controller_ = nullptr;
    std::vector<OverviewShot> overviews_;
    std::vector<StackEntry>   stack_;
    MoveState                 move_{};
};

/** @brief Register CinemaDirector EveScript bindings on the camera module table. */
void exposeCinemaDirectorBindings(ssq::Table& table);

}  // namespace eve::camera
