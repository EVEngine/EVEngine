#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include "avatar/VrmDocument.h"
#include "common/ECS.h"

namespace eve::animation {
class AnimSkeleton;
class AnimPlayer;
class AnimSkin;
class AnimPose;
}  // namespace eve::animation
namespace eve::model3d {
class ModelData;
}
namespace eve::graphics {
class Graphics;
class Mesh;
class Material;
class Shader;
class Texture;
}  // namespace eve::graphics
namespace eve::avatar {
class AvatarInstance;
struct VrmSurface;

// Private import transaction and runtime owner. ECS projections are always resolved by generation.
class VrmRuntime {
public:
    VrmRuntime();
    ~VrmRuntime();
    static eve::Result<std::unique_ptr<VrmRuntime>> prepare(std::string_view path);
    void animate(AvatarInstance& avatar, animation::AnimPose& pose, float dt);
    void sync(AvatarInstance& avatar, const glm::mat4& world, bool visible);
    void morphs(AvatarInstance& avatar);
    void gaze(AvatarInstance& avatar, animation::AnimPose& pose, const glm::mat4& world, bool enabled, glm::vec3 target,
              float weight);
    std::weak_ptr<const void>                providerLifetime;
    VrmDocument                              document;
    std::unique_ptr<model3d::ModelData>      model;
    std::unique_ptr<animation::AnimSkeleton> skeleton;
    std::unique_ptr<animation::AnimPlayer>   player;
    std::vector<int>                         nodeBones;
    std::map<int, std::array<float, 3>>      rotations;
    struct Part {
        ecs::EntityHandle                    entity, outline;
        graphics::Mesh*                      mesh       = nullptr;
        int                                  sourceMesh = -1, node = -1, material = -1;
        std::unique_ptr<animation::AnimSkin> skin;
        std::vector<float>                   cpu;
    };
    std::vector<Part>                        parts;
    std::vector<std::unique_ptr<VrmSurface>> surfaces;
    std::vector<graphics::Texture*>          textures;
    glm::mat4                                world{1};
    float                                    time = 0;

private:
    struct Particle {
        glm::vec3 current{}, previous{};
        bool      initialized = false;
    };
    std::vector<std::vector<Particle>>     particles_;
    std::unordered_map<std::string, float> gazeWeights_;
    void                                   springs(animation::AnimPose& pose, float dt);
};
}  // namespace eve::avatar
