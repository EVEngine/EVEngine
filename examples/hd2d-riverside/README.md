# HD2D 河畔场景验收

这是使用真实像素素材的可运行 HD2D 功能验证场景：22×16 正交地图、16 种地面图块、
两层独立地形、河流/桥梁/高台、真实 OBJ 木框房屋，以及冒险者、学者、卫兵三名角色。
每名角色为 64×64 单元、6 列×4 行，行顺序为 south/east/north/west。

```sh
make run/win32-debug GAME=examples/hd2d-riverside
```

WASD 或方向键移动，1/2/3 切换操控角色，Q/E 环绕相机。未选中的角色自动巡走。
方向键使用世界 X/Z 轴；相机环绕不改变世界方向。每次只沿一个方向移动。
河岸、水面和高台阻挡移动；木桥可以通行。相机显式设置 1..3000 的裁剪范围。

## 数据与素材

- `build_assets.py` 是地图布局的生成源，输出 `assets/riverside.json` 与运行时的
  `map-data.nut`；调整布局后运行一次该脚本，两份输出不要独立修改。
- 地形 atlas 为 `assets/terrain.png`。地面与高台作为不同 TileLayer 烘焙，碰撞读取
  同一份地图数据，不根据截图推导几何。地图有 schema/version，当前生成器只输出 v1。
- `assets/{hero,scholar,guard}/64/final/walk-sheet-clean.png` 是运行时角色图集；
  相邻的 `walk-metadata.json` 描述帧布局。脚底保留 4px 透明边距，使用
  `setPivot(0.5,60.0/64.0)`、地面 y=0。人物跟随相机俯仰、偏航和滚转，保持屏幕对齐。
- `assets/house/cottage.obj` / `cottage.mtl` 是原创、可独立编辑的房屋资产，10 个材质组、
  3350 个三角形，带屋瓦、坡屋顶、烟囱、门窗和木框。MTL 引用 `../terrain.png`；
  复制模型时应保留这个相对目录关系。通过 `model3d` 导入，没有运行时程序拼装房屋。
  运行 `python build_house.py` 可确定性重建；材质与建模尺寸的权威来源是该脚本。
  raised 层的 GID 10 标记房屋碰撞占地、确定模型中心，不再挤出；GID 9 高台仍照常挤出。
- 美术使用内置 imagegen 制作：每名角色先生成身份参考，再独立生成四个方向条带。
  `source`、`64/prompts`、`run-manifest.json` 保留原图、完整提示词和来源记录。
  脚本只负责切帧、透明清理、装配和地图数据，没有程序绘制角色或地面原画。
- 每名角色的 `64/qa` 含几何检查、动画帧差检查、人工验收和四方向 GIF/WebP。
  `assets/cast-preview.webp` 是三个角色的合成动画预览。
- 重建地图和合成预览需要 Python + Pillow：
  `python build_assets.py`、`python preview_cast.py`。

## 本次实景发现并修复

1. `Sprite3D.setCamera` 文档存在但脚本未绑定，已补上。
2. 精灵的可见 UV 上下颠倒且左右镜像，已统一为图像左上角对应可见左上角。
3. 精灵没有 masked 材质，透明背景显示成黑色矩形并写入错误深度。现在使用 Graphics
   拥有的 masked 材质，颜色、G-buffer 和阴影使用一致的裁切路径，换纹理/颜色同步到材质。
4. TileMap3D 把 tile 起点当中心，导致地图与碰撞相差半格。现在 footprint 与 map 坐标一致。
5. 地形面绕序与法线相反，已修复。
6. 烘焙丢弃 Tiled H/V/对角翻转信息，已修复顶面采样；八种组合用像素测试覆盖。
7. 零厚度地形仍生成侧面与重叠底面，改为只生成顶面。
8. 更换帧网格后旧动画仍运行，改为停止旧 clip；网格乘法溢出和非有限 fps 会被拒绝。
9. 负数/NaN/Infinity/超大 dt 会污染或溢出动画时钟，已使用有限增量和周期约简。
10. 旧 flip 测试错误地期待切换到另一个 atlas 单元，而且仅用 CHECK。已改为 REQUIRE
    验证选中区域不变，并以非对称颜色和透明角落测试实际翻转、方向与裁切。
11. Vulkan G-buffer pipeline 的依赖掩码与实际 FrameGraph render pass 不兼容，已对齐。
12. 阴影数组的最终布局与采样描述符不一致，已统一为 shader-read-only。
13. 多物体的动态 UBO 描述符错误地覆盖整个环形缓冲区，偏移后越界；普通和 clustered
    路径均改为单个 UBO 的范围。实际场景和像素测试在修复前复现了校验错误。
14. 圆柱 billboard 只绕 Y 轴旋转，俯视时人物压扁。改为相机完整图像平面基向量，并增加
    pivot 以保持脚底锚点；离轴、俯仰、滚转、环绕、缩放和更新 UV 用实际像素测试覆盖。
15. 模型外部贴图路径被当成 VFS 根路径，OBJ 的四组贴图全部丢失。优先相对模型 URI
    解析并正规化路径，保留既有根路径兼容查找；场景新增检查四组实际加载的 albedo。
16. 近距离镜头下角色进入阴影投射路径，暴露 shadow pipeline 与 FrameGraph 的依赖数量
    不一致；两种阴影管线共用的 render pass 已对齐实际录制路径。

## 验证证据

- 新增的五项地图/动画回归在修复前全部失败、修复后通过。
- 非对称精灵像素测试分别复现了倒置和镜像，修复后验证透明角落与双轴翻转。
- HD2D 定向 CTest 17/17 通过；开启 Vulkan validation 后仍为 17/17、0 validation errors。
  可运行 `python examples/hd2d-riverside/validate_vulkan.py build/win32-debug`：脚本会额外
  检查校验层确实启用，并把日志中的 validation error 当作失败，避免只有 CTest 绿灯。
- 场景内调用 `(function(){ dofile("verify.nut"); return verifyRiverside(); })()`，
  返回 `HD2D_SCENE_PASS checks=71`：三名角色的四方向、动画/停止、河岸、高台、桥，
  以及 OBJ 材质组数量和四组外部贴图实际加载。
- 通过引擎 `eve_screenshot` 读取真实 Vulkan 帧，确认平台前/后遮挡、正向角色和无黑边框。
  最终实景开启校验同样为 0 validation errors；截图见 `qa/runtime.png` 和 `qa/house-orbit.png`。
  屏幕对齐的图像平面仍参与真实深度测试；靠近实体时若平面与其相交，会按深度裁切，
  不会强制把整个人物画到遮挡物前面。脚底锚点用于贴地，不等同于图像碰撞体。
- 三份图集几何、残留色、运动差异和来源验证全部通过，均为 0 warnings。
- 架构契约检查及其 7 个 fixtures、模块依赖检查、diff whitespace 检查通过。

本机验证使用独立 `build/win32-debug`，MSVC，`minimal + hd2d + font + devtools + model3d` 裁剪组合。
当前仓库的通用 unit_test 仍收集被裁剪模块的测试，因此本次通过本地 CMake deferred hook
将 runner 限定为 `test/main.cpp`、`test/hd2d.cpp` 和两份 `hd2d_*_contract.cpp`。
没有修改全局测试收集策略，也没有验证完整引擎或 WebGPU/移动后端。

本机预编译依赖来自 `761fc836ebcc37820c779e0c8ee30a975bd0d148`，为避免头文件/库混用，
本地 dependency checkout 与 CMake cache 使用同一提交；仓库锁定的
`54725d0ae7c4a3092dc16841e0b0064f03365a45` 未修改。正式 CI 的锁定依赖仍需验证。

## 架构边界与完整性范围

本次没有新增公共对象根、跨域 Link、ECS System 或持久化引擎格式。修复复用 map 的坐标、
graphics 的材质/深度管线和 HD2D 的既有对象。所有 GPU 修改在渲染线程执行；Graphics
拥有网格/材质/纹理，Sprite 与 Camera 遵循既有 ECS 生命周期。动画由调用方注入 dt；
场景模拟采用 60 Hz 固定步长，无 RNG。没有新增隐式 provider 或反向依赖，也没有豁免规则。

这里验证的是正交、单 atlas、静态烘焙场景的完整工作路径。地图编辑后需要重新烘焙；高度仍
是 GID 元数据。大型世界分块/流式加载、同 GID 逐格高度、多个 atlas 合批、动态水 tile 动画、
任意投影地形几何与跨后端一致性，不应从本示例推断为已实现或已验收。
