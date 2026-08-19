#pragma once

// 方块类型定义：名字、各面图集纹理、方向性、组合声明。
// 方向性方块在注册时按 orientation 绕 Y 轴展开成多个“具体类型”变体，
// 每个变体持有旋转后的各面纹理；渲染端只消费纹理 id，不接触 orientation。

#include <cstdint>
#include <string>

namespace eve::voxel {

struct CubeType {
    std::string name;
    uint8_t id = 0;            // 0 保留为空气；1..255 类型索引（含方向变体）
    uint8_t faceTex[6] = {};   // 各面图集纹理索引，按 FaceDir 顺序 PosX..NegZ
    bool directional = false;  // 是否可绕 Y 轴旋转
    std::string composeGroup;  // 同组可组成更大结构，如 "door" / "bed"（行为后置）
    bool connects = false;     // 连接纹理提示（保留）
};

}  // namespace eve::voxel
