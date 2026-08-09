#pragma once

#include "avatar/AvatarInstance.h"
#include "common/Module.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::avatar {

/**
 * Avatar module — layered character rendering for VN / portrait use.
 * Script: `avatar <- eve.Avatar();`
 *
 * Factories: newImageAvatar / newLive2DAvatar / newVroidAvatar.
 * Image layers are the baseline; Live2D uses a pluggable backend; VRoid
 * wraps Model3D + Renderable3D.
 */
class Avatar : public Module {
public:
    Module_REG(Avatar);
    Avatar() = default;
    ~Avatar() override;

    AvatarInstance *newImageAvatar();
    AvatarInstance *newLive2DAvatar();
    AvatarInstance *newVroidAvatar();

    void update(float dt);
    /** Sync all live avatars into renderables. */
    void sync();
    /**
     * Sync then draw Image/Live2D avatars via the shared 2D sprite queue
     * (does not present). VRoid uses Renderable3D + gfx.render3D separately.
     */
    void render(graphics::Graphics *gfx);
    int getAvatarCount() const;

    /** C++ / plugin: replace Live2D backend factory (nullptr clears). */
    static void registerLive2DBackend(Live2DBackendFactory factory);
    static Live2DBackendFactory live2DBackendFactory();
    static ILive2DBackend *createLive2DBackend();

private:
    friend class AvatarInstance;
    void registerInstance(AvatarInstance *a);
    void unregisterInstance(AvatarInstance *a);

    std::vector<AvatarInstance *> avatars_;
    static Live2DBackendFactory live2dFactory_;
};

}  // namespace eve::avatar
