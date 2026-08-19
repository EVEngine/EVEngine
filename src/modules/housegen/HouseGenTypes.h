#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::housegen {

/** @brief 组件连接点方向（上下 + 四向）。 */
enum class SocketDirection { North, East, South, West, Up, Down };

/** @brief 组件连接点：方向 + 类型 + 可接受类型。 */
struct HouseSocket {
    SocketDirection direction = SocketDirection::North;
    std::string type;
    std::vector<std::string> accepts;
};

/** @brief 组件材质覆盖（未设置的字段沿用默认材质）。 */
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

/** @brief 房屋组件模板（几何 + 连接点 + 权重）。 */
struct HouseComponent {
    std::string id;
    std::string modelPath;
    std::string category;
    /** @brief 占地尺寸（格）。 */
    int width = 1;
    int depth = 1;
    int height = 1;
    /** @brief 生成权重（越大越常被选中）。 */
    int weight = 1;
    /** @brief 允许的旋转（度）。 */
    std::vector<int> rotations {0, 90, 180, 270};
    std::vector<std::string> tags;
    /** @brief 连接点。 */
    std::vector<HouseSocket> sockets;
    /** @brief 材质覆盖。 */
    HouseMaterialOverride material;
};

/** @brief 一次房屋生成请求（参数集）。 */
struct HouseRequest {
    uint32_t seed = 1;
    /** @brief 占地尺寸（格）与层数。 */
    int width = 6;
    int depth = 6;
    int floors = 1;
    /** @brief 模块尺寸 / 层高（世界单位）。 */
    float moduleSize = 1.f;
    float floorHeight = 3.f;
    std::string style;
    /** @brief auto | rectangle | l_shape | t_shape。 */
    std::string footprint = "auto";
    /** @brief auto | gable | flat | shed。 */
    std::string roof = "auto";
    /** @brief auto | north | east | south | west。 */
    std::string entrance = "auto";
    std::vector<std::string> requiredRooms {"living"};
    /** @brief 生成重试上限。 */
    int maxAttempts = 32;
};

/** @brief 布局中的单个组件实例（位置 + 旋转）。 */
struct HouseInstance {
    std::string componentId;
    /** @brief 格子坐标（x,y,z = 层）。 */
    int x = 0;
    int y = 0;
    int z = 0;
    /** @brief 旋转（度）。 */
    int rotationDeg = 0;
};

/** @brief 布局中的房间标记。 */
struct HouseRoom {
    std::string type;
    /** @brief 格子坐标与尺寸。 */
    int x = 0;
    int y = 0;
    int width = 1;
    int depth = 1;
};

}  // namespace eve::housegen
