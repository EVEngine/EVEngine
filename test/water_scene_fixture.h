#pragma once

#include <vector>

namespace eve::graphics {
class Camera3D;
class Graphics;
class Renderable3D;
}

struct WaterSceneFloater {
    eve::graphics::Renderable3D* renderable = nullptr;
    float                        x          = 0.0F;
    float                        y          = 0.0F;
    float                        z          = 0.0F;
    float                        phase      = 0.0F;
    float                        roll       = 0.0F;
};

struct WaterSceneFixture {
    std::vector<WaterSceneFloater> floaters;

    void update(float time);
};

WaterSceneFixture createStylizedWaterScene(eve::graphics::Graphics* gfx, eve::graphics::Camera3D* camera);
