#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::housegen {

enum class SocketDirection { North, East, South, West, Up, Down };

struct HouseSocket {
    SocketDirection direction = SocketDirection::North;
    std::string type;
    std::vector<std::string> accepts;
};

struct HouseMaterialOverride {
    bool hasBaseColor = false;
    float baseColorR = 1.f;
    float baseColorG = 1.f;
    float baseColorB = 1.f;
    float baseColorA = 1.f;
    std::string baseColorTexture;
    std::string normalTexture;
    std::string heightTexture;
    bool hasMetallic = false;
    float metallic = 0.f;
    bool hasRoughness = false;
    float roughness = 1.f;
    float parallaxScale = 0.f;
    float parallaxMinLayers = 8.f;
    float parallaxMaxLayers = 32.f;
    float cellBombScale = 4.f;
    float cellBombStrength = 0.f;
    float cellBombRotation = 1.f;
};

struct HouseComponent {
    std::string id;
    std::string modelPath;
    std::string category;
    int width = 1;
    int depth = 1;
    int height = 1;
    int weight = 1;
    std::vector<int> rotations {0, 90, 180, 270};
    std::vector<std::string> tags;
    std::vector<HouseSocket> sockets;
    HouseMaterialOverride material;
};

struct HouseRequest {
    uint32_t seed = 1;
    int width = 6;
    int depth = 6;
    int floors = 1;
    float moduleSize = 1.f;
    float floorHeight = 3.f;
    std::string style;
    std::string footprint = "auto";  // auto, rectangle, l_shape, t_shape
    std::string roof = "auto";       // auto, gable, flat, shed
    std::string entrance = "auto";   // auto, north, east, south, west
    std::vector<std::string> requiredRooms {"living"};
    int maxAttempts = 32;
};

struct HouseInstance {
    std::string componentId;
    int x = 0;
    int y = 0;
    int z = 0;
    int rotationDeg = 0;
};

struct HouseRoom {
    std::string type;
    int x = 0;
    int y = 0;
    int width = 1;
    int depth = 1;
};

}  // namespace eve::housegen
