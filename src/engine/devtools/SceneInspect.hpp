#pragma once

#include "common/Export.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eve {
class IRenderCapture;
class ISceneQuery;
}  // namespace eve

namespace eve::dev {

/**
 * @brief 场景巡检工具集（MCP 底层数据底座）。
 *
 * 提供原子化的相机控制 + 渲染帧捕获 + 可见实体三维几何快照，保证图像与
 * 3D 几何数据严格同步（同一次相机位姿下抓帧并导出配套几何 JSON）。
 *
 * 本模块不依赖任何脚本状态，直接读取引擎的 Graphics / Camera3D / Scene
 * 运行时模块，因此可被 MCP 工具、C++ 测试或调试面板复用。
 *
 * 桌面端专用（EVDevTools 的一部分）。
 */
struct EVENGINE_API InspectView {
    std::string name;   // "along_road" | "bird_eye" | "corner_close" | "vista"
    std::string kind;   // 中文说明：沿路平视 / 高空俯拍 / 拐角特写 / 远景视角
    glm::vec3   eye{0.f};
    glm::vec3   target{0.f};
    float       fovYDeg = 60.f;
};

/** @brief 一次原子快照的结果：PNG 渲染帧 + 配套几何 JSON。 */
struct EVENGINE_API InspectCapture {
    bool        ok = false;
    std::string error;
    std::string pngPath;   // 渲染帧 PNG 的磁盘路径（color buffer）
    std::string jsonPath;  // 配套三维几何快照 JSON 的磁盘路径
    int         width  = 0;
    int         height = 0;
    int         entityCount = 0;
    // 可选附加缓冲区（按需生成）。
    std::string depthPngPath;   // depth buffer PNG（GBuffer 线性深度）
    std::string normalPngPath;  // normal buffer PNG（GBuffer 世界法线）
    std::string idPngPath;      // 渲染 ID mask PNG（逐像素实体 ID 颜色）
    std::string idJsonPath;     // 渲染 ID mask 的 id→实体 映射 JSON
    std::vector<std::string> unsupported;  // 请求但当前后端不支持的缓冲区
};

class EVENGINE_API SceneInspect {
public:
    static SceneInspect& instance();

    SceneInspect(const SceneInspect&)            = delete;
    SceneInspect& operator=(const SceneInspect&) = delete;

    // ---------- 多机位自动采样 ----------
    /**
     * @brief 围绕 center 自动生成一组标准化巡检机位：
     *   - along_road  沿路平视：低机位、贴近目标、视线水平略前倾
     *   - bird_eye    高空俯拍：目标正上方垂直俯拍
     *   - corner_close 拐角特写：贴近目标、斜向特写、较窄 FOV
     *   - vista       远景视角：抬高拉远、广角俯览
     * fov 为基准 FOV（度），各机位在此基础上微调。
     */
    static std::vector<InspectView> generateViews(const glm::vec3& center, float fov = 60.f);

    // ---------- 相机位姿 ----------
    /**
     * @brief 以 pos + 欧拉角 rot 设置相机位姿。
     * rot = [yawDeg, pitchDeg]（与 firstperson 约定一致，Y-up）。
     * fov <= 0 时保持当前 FOV。
     */
    bool setCameraPose(const glm::vec3& eye, const glm::vec3& rotYawPitch, float fov = 0.f);
    bool setCameraView(const InspectView& v);

    // ---------- 原子快照（渲染帧 + 几何 JSON） ----------
    /**
     * @brief 锁定当前相机 → 捕获渲染帧 PNG → 立刻导出配套可见实体几何 JSON。
     * 两者共享同一相机位姿，杜绝图片与实体数据错位。
     * outDir 为空时使用缓存目录。返回文件路径与图像尺寸。
     * buffers 可选值："color"(默认, 帧) | "depth" | "normal" | "id"(渲染 ID mask)
     *  | "shadow"(当前后端不支持, 记为 unsupported)。
     */
    InspectCapture capture(const std::string& outDir = {}, const std::string& tag = "frame",
                           const std::vector<std::string>& buffers = {});

    // ---------- 可见实体三维几何快照 ----------
    /**
     * @brief 生成当前视锥内可见实体的结构化 JSON：
     *   { camera, viewport, entities:[ { id, asset, world_aabb:{min,max},
     *     screen_bbox:{x,y,w,h} } ] }
     * 位姿可用 camera 指定，否则回退到当前激活相机。
     */
    std::string visibleEntitiesJson(const glm::vec3* eye = nullptr,
                                    const glm::vec3* target = nullptr, float fov = 0.f);
    /** @brief 当前激活相机位姿 JSON（eye/target/fov/viewport）。 */
    std::string currentPoseJson();

    // ---------- 快照持久化 / 临时缓存 ----------
    void setCacheDir(std::string dir) { cacheDir_ = std::move(dir); }
    const std::string& cacheDir() const { return cacheDir_; }
    /** @brief 默认缓存目录：<gameRoot>/eve_inspect_cache。 */
    std::string defaultCacheDir() const;
    /** @brief 最近一次快照的 PNG / JSON 路径（供后续读取）。 */
    const std::string& lastPngPath() const { return lastPngPath_; }
    const std::string& lastJsonPath() const { return lastJsonPath_; }

private:
    SceneInspect() = default;

    eve::IRenderCapture* captureIface();
    eve::ISceneQuery*    scene();
    bool                 ensureCamera();

    std::string cacheDir_;
    std::string lastPngPath_;
    std::string lastJsonPath_;
};

}  // namespace eve::dev
