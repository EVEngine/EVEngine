#pragma once

#include <cstdint>
#include <string>

namespace eve::voxel {

/**
 * @brief Six axis-aligned face directions. Each chunk keeps a separate instance buffer
 * per direction so camera-facing sides can be culled / batched independently.
 *
 * Tangent axes for packed width/height:
 *   ±X : width along Z, height along Y
 *   ±Y : width along X, height along Z
 *   ±Z : width along X, height along Y
 */
enum class FaceDir : uint8_t {
    PosX = 0,
    NegX = 1,
    PosY = 2,
    NegY = 3,
    PosZ = 4,
    NegZ = 5,
    Count = 6,
};

inline constexpr int faceDirCount() { return int(FaceDir::Count); }

inline const char *faceDirName(FaceDir d) {
    switch (d) {
        case FaceDir::PosX: return "posX";
        case FaceDir::NegX: return "negX";
        case FaceDir::PosY: return "posY";
        case FaceDir::NegY: return "negY";
        case FaceDir::PosZ: return "posZ";
        case FaceDir::NegZ: return "negZ";
        default: return "";
    }
}

inline bool faceDirFromName(const std::string &name, FaceDir &out) {
    if (name == "posX" || name == "+x") {
        out = FaceDir::PosX;
        return true;
    }
    if (name == "negX" || name == "-x") {
        out = FaceDir::NegX;
        return true;
    }
    if (name == "posY" || name == "+y") {
        out = FaceDir::PosY;
        return true;
    }
    if (name == "negY" || name == "-y") {
        out = FaceDir::NegY;
        return true;
    }
    if (name == "posZ" || name == "+z") {
        out = FaceDir::PosZ;
        return true;
    }
    if (name == "negZ" || name == "-z") {
        out = FaceDir::NegZ;
        return true;
    }
    return false;
}

/**
 * @brief 绕 Y 轴旋转一个面方向 `quarterTurns` 个 90°（右手定则：+X → -Z → -X → +Z）。
 * 仅影响 4 个水平面（±X/±Z），±Y 顶/底保持不变。
 * 供方向性方块在注册各面纹理时旋转使用。
 */
inline FaceDir rotateFaceY(FaceDir dir, int quarterTurns) {
    if (dir == FaceDir::PosY || dir == FaceDir::NegY) return dir;
    static const FaceDir cycle[4] = {FaceDir::PosX, FaceDir::NegZ, FaceDir::NegX, FaceDir::PosZ};
    const int t = quarterTurns & 3;
    for (int i = 0; i < 4; ++i) {
        if (cycle[i] == dir) return cycle[(i + t) & 3];
    }
    return dir;
}

/** @brief Outward unit normal for the face. */
inline void faceNormal(FaceDir d, float &nx, float &ny, float &nz) {
    nx = ny = nz = 0.f;
    switch (d) {
        case FaceDir::PosX: nx = 1.f; break;
        case FaceDir::NegX: nx = -1.f; break;
        case FaceDir::PosY: ny = 1.f; break;
        case FaceDir::NegY: ny = -1.f; break;
        case FaceDir::PosZ: nz = 1.f; break;
        case FaceDir::NegZ: nz = -1.f; break;
        default: break;
    }
}

}  // namespace eve::voxel
