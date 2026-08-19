#pragma once

#include "housegen/HouseGenTypes.h"

#include <string>
#include <vector>

namespace eve {
namespace graphics { class Graphics; class Renderable3D; }
namespace model3d { class Model3D; }
namespace housegen {

class HouseComponentLibrary;

class HouseLayout {
public:
    uint32_t seed = 1;
    float moduleSize = 1.f;
    float floorHeight = 3.f;
    std::string footprintStyle = "rectangle";
    std::string roofStyle = "gable";
    std::string entranceSide = "north";
    std::vector<HouseInstance> instances;
    std::vector<HouseRoom> rooms;
    std::vector<std::string> diagnostics;

    void clear();
    std::string toJson() const;
    bool fromJson(const std::string &json, std::string *error = nullptr);
    bool validate(const HouseComponentLibrary &library, std::string *error = nullptr) const;
    std::vector<graphics::Renderable3D *> instantiate(graphics::Graphics *gfx,
                                                        model3d::Model3D *models,
                                                        const HouseComponentLibrary &library,
                                                        std::string *error = nullptr) const;
};

}  // namespace housegen
}  // namespace eve
