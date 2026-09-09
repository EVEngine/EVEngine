# Unity 资源导入

当前实现提供 `.unitypackage` 读取、Unity 项目资源索引和有限的批量 canonical 转换。
**资源被识别或原文件被保留，不表示其运行时行为已转换。**

## 使用

先检查本地购买资源包，或带 `.meta` 的资源目录：

```sh
eve asset scan MyAssets.unitypackage
eve asset scan path/to/UnityProject
```

扫描输出 `eve.unity-source-index/1` JSON，包含 path、GUID、资源类别、importer 名称、
GUID/fileID 引用和缺失依赖诊断。它是只读诊断输出，不是运行时资源格式；后续消费者必须
检查 schema/version，拒绝未知版本，可以忽略不认识的诊断字段。

批量转换无需逐个指定 Prefab：

```sh
eve asset import MyAssets.unitypackage --from unity --out MyAssets.eva --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31040 --name my-assets --version 1.0.0
eve asset validate MyAssets.eva
eve asset cook MyAssets.eva --target windows-x86_64-vulkan --out MyAssets.evpack
```

示例 UUID 仅用于演示；项目应为每个逻辑资源包分配自己的 UUID，并在后续重导入保持该值。
源文件移动/重命名时必须一起保留 `.meta`，这样导入产生的 AssetRef 保持稳定。
命令行扫描完整 Unity 项目时只读取其 `Assets/` 子树，不扫描 `Library/`、`Temp/` 等缓存。
目录输入必须保留原始路径结构和 `.meta`；丢失依赖时扫描会列出对应 GUID/fileID。

已有的 `--prefab relative/path.prefab` 与 `--terrain relative/path.asset` 显式选择入口继续可用。
指定选择器时保持原有单项转换语义；只有不指定两者时执行 collection 导入。

## 当前转换边界

- `.unitypackage`：gzip/tar、GUID 目录、pathname、asset、asset.meta 和可选 preview.png。
  支持常规文件、文件夹元数据以及 `./` 前缀，不写临时解包目录。
- 分类索引：Prefab、Scene、模型、材质、图像、Animation/Controller、音频、字体、
  Shader、脚本和数据资源。fileID 使用有符号 64 位值；内置 Unity GUID 不当作丢失文件。
- PNG/JPEG：复用现有 `eve.image/2` 导入。读取 `sRGBTexture`，normal map 按 linear
  标记。现有 PNG Cook 链路可用；JPEG 是否可 Cook 仍受已有 image Cook 编码支持限制。
- glTF/GLB：复用当前三角网格导入器，依赖文件从所在目录的相对路径解析。
- FBX：桌面 Assimp 解码静态三角网格，按 Unity `fileIdsGeneration: 2` 的节点名称
  哈希恢复子资源 fileID。当前要求节点名称唯一、每节点一个 mesh、导入原始 normals/UV0、
  正数 globalScale 和 file scale。轴向转换为 `(-x,y,-z)`，UV 转为 `(u,1-v)`，
  单位比例为 `UnitScaleFactor * 0.01 * globalScale`。旧 fileID、蒙皮和动画明确不支持。
  Web 没有 Assimp provider 时明确报告 Unsupported。额外顶点流不被伪装成已转换。
- Material：内置 Standard 的 opaque metallic/roughness、baseColor、主贴图及其依赖；
  非单位贴图变换、自定义 shader 和透明模式尚未转换。其他保存属性/关键字单独报告。
- Prefab：直接 GameObject/Transform 层级、可见性，以及 GUID/fileID 可准确解析的单材质
  MeshFilter/MeshRenderer 绑定。Collider、RectTransform 布局、脚本和其他组件逐项报告。
- 批量输出：支持的 canonical 资产进入 `.eva`；原始文件及 `.meta` 保存在
  `sources/unity/` 作为不可变来源记录，不作为可执行组件或第二份可变领域状态。
  `reports/import.json` 记录源文件哈希、转换结果和未支持项。只有未支持资源的包会失败，
  不发布一个看似成功的空资源包。

尚未完成：多材质子网格路由、嵌套 Prefab/Variant 覆盖、角色蒙皮与动画、
独立 Sprite 资产、九宫格/UI 行为、Animator 状态机、AudioClip/字体 canonical 转换，以及编辑器拖放/预览流程。
这些属于后续保真转换工作，不能以本阶段的索引或源文件保留代替验收。

### 2D 精灵动画

支持 `.anim` 中单个根 SpriteRenderer 的 `m_Sprite` 引用轨道。按 GUID 和有符号
fileID 解析 PNG `.meta` 的 multiple Sprite 切片，保留矩形、居中/自定义锚点、
pixels-per-unit、nearest/linear 过滤，以及 clip 时长、循环和原始关键帧顺序。
切片坐标从 Unity 左下转换为左上；alignment=0 按实际居中语义解释，不能使用
可能仍为 `{0,0}` 的存储 pivot。关键帧按 sampleRate 的时间格规范化（容差 1e-5 秒），
末帧一直保持到 stopTime，循环边界返回首帧。

`eve.sprite-animation/1` 是新资产 schema，容器版本不变；没有旧版可迁移，未知版本
拒绝、未知字段忽略。定义包含 duration、loop、sampleRate 和 frames：每帧含 time、
image AssetRef、name、imageSize、rect、归一化 pivot、pixelsPerUnit、filter。
frame 的 sourceGuid/sourceFileId 仅为不可变来源记录，运行时依赖由 image 引用决定。
`SpriteAnimationClip::load/decode` 返回独立 owning 候选；`sample(seconds)` 返回 owning
帧快照，显式注入时间，既不推进时钟也不创建 ECS/图形对象，可在 worker 并发只读调用。
已有候选不因失败重导入改变。缺失帧引用、非法矩形和时间顺序会在发布前失败。
非 Sprite 曲线、动画事件、多轨道、非根路径及 Animator controller 行为尚未转换。

Evil Wizard 本地样本验证：5 张 PNG、1 个 authored clip，33 个关键帧，12 FPS、
2.75 秒循环，依次为 Idle(8)、Move(8)、Attack(8)、Take Hit(4)、Death(5)。
这五段原本就在同一个 clip 内，不自动虚构五个独立动作或状态机。
正式导入与 Cook 生成 6 个资产、11 个运行时 chunk；Vulkan 60 Hz 采样及 Canvas 回读
覆盖 166 帧（含循环边界），没有 validation error。回归夹具仅使用合成 PNG/metadata。

```sh
cmake --build build/win32-debug --target unity_sprite_graphics_probe
glslc test/asset_import/UnityPreviewDisplay.frag -o build/unity-preview-display.frag.spv
unity_sprite_graphics_probe EvilWizard.evpack build/wizard-frames build/unity-preview-display.frag.spv
```

该手工探针消费生产运行时 sampler 和 image loader，显示原始 PNG 裁切及像素过滤，
最多导出 60 秒，以引擎回读的 PNG 序列验证播放；它不代替 Animator 转场验收。

## 契约与错误处理

`readUnityPackage` / `indexUnitySources` 同步读取借用输入，返回独立拥有数据的 Result。
输入在调用期间不可修改；没有全局状态、脚本执行、回调或外部网络访问，可在 worker 调用。
prepare 不修改项目/数据库；成功候选仍由现有 AtomicAssetPackageStore 发布。解析或
转换失败不调用发布，因此旧包保持原状。此变更不引入 ECS System、跨域运行时 Link，
不修改 `.eva` / `.evpack` 容器 schema。scene-template 资产升级到版本 2：
`renderers` 数组的每项包含持久 `objectId`、`mesh`/`material` AssetRef 与 `enabled`。
版本 1 经 migration 添加空 renderers，保留未知字段；版本 2 消费者忽略未知字段但拒绝
未知版本、重复对象、悬空节点、非法引用及版本 1 中未版本化的 renderers。
`eve.material/1` 定义 `shadingModel=pbr`、`surfaceMode=opaque`、四分量 baseColor、
归一化 metallic/roughness 和可选 baseColorTexture；未知字段忽略，未知版本拒绝。

`EvpackStaticPrefab::load` 消费 scene-template、mesh、material、image 并返回独占 owning
候选；`draw` 在现有 3D pass 中提交静态 PBR 绘制。创建/绘制/释放/析构均在 graphics
线程，mesh/image factory 必须活到候选释放之后。失败清理本次已上传资源，不修改旧候选；
重导入先 load 新候选再交换。`release` 可以重试失败释放，析构报告未释放错误。
该入口不创建 ECS/Scene Link，也不自动提交 shadow-caster pass，未支持项会记录在报告中。

包读取校验 gzip 完整性、tar checksum/边界/结束块、解压预算、路径和源数量。
重复条目、重复 GUID、ASCII 大小写路径冲突、绝对/越界路径、链接和不支持的 tar
扩展记录显式失败。PAX/GNU 扩展记录尚未支持，不能宣称覆盖所有历史 Unity 导出器。
源索引报告缺失 GUID 依赖，但不声称已经解析或实例化所有 fileID 子资源。

## 验证

单元测试在 `test/asset_import_unity_package.cpp` 和
`test/asset_import_unity_collection.cpp`，标准全量 CTest 会自动发现它们。
CPU-only 入口 `test/asset_import/CMakeLists.txt` 使用相同生产源码和测试，便于不构建
图形宿主时验证解析、canonical 归档和 Cook。需要 zeroerr 子模块，以及匹配的 zlib、
utf8proc、zstd、Assimp、xxHash 开发库；CLI 编译检查还需要 CLI11.hpp。

```sh
cmake -S test/asset_import -B build/unity-source-tests
cmake --build build/unity-source-tests
ctest --test-dir build/unity-source-tests --output-on-failure
```

Windows 通过 `cmake/with-msvc.cmd` 执行 CMake，并指定 Ninja、Debug 和
`msvc-cl.cmd` 编译器；可用 `ZLIB_INCLUDE_DIR/ZLIB_LIBRARY`、
`UTF8PROC_INCLUDE_DIR/UTF8PROC_LIBRARY`、`ZSTD_INCLUDE_DIR/ZSTD_LIBRARY`、
`CLI11_INCLUDE_DIR` 指定已经安装的依赖。不要为了通过完整引擎配置而绕过 third-party
版本一致性校验。

合成测试覆盖压缩包错误输入、分类/引用索引、GUID 稳定性、批量导入、
源记录保留及 `.eva → Cook → .evpack`。它们不替代真实购买资源的外观、动画、
音频播放和 UI 行为验收。

手工验证本地购买包可运行 `unity_package_probe <unitypackage>`，它调用同一套生产
解析、导入、归档和 Cook 代码，在内存中校验结果并打印未支持项，不发布或写回原包。
真实 HYPEPOLY Isometric Tiles Standart Lite 样本已验证：113 个源条目，转换出
23 个 mesh、4 个材质、4 张图像及 92 个带渲染绑定的 Prefab，生成并校验 150 个运行时 chunk。
使用独立 Unity 6 项目导出参照后，23 个 mesh fileID 全部一致；统一轴向后全部顶点位置
一致（允许 Unity 焊接/重排）。记录后端验证 92 次 mesh/image 上传及对应释放。
正式 `eve asset import` 也验证了 Unity 已导入的资源子目录：使用相同 package ID 时，
输出的全部 150 个 canonical 文件与原始 unitypackage 转换结果逐字节一致。
原生 Vulkan 离屏验收实际绘制了全部 92 个 Prefab，并由引擎 Canvas 回读 PNG；
启用 Khronos 验证层后没有 validation error，仍有未使用顶点属性的 performance warning。
碰撞/阴影投射、原始 Unity 场景布局和动画仍未支持。
购买素材仅放在本机忽略的 build 目录；回归测试使用合成数据。

可复现的手工图形验收入口（需要完整引擎依赖和 Vulkan）：

```sh
cmake --build build/win32-debug --target unity_graphics_probe
glslc test/asset_import/UnityPreviewDisplay.frag -o build/unity-preview-display.frag.spv
build/win32-debug/test/unity_graphics_probe MyAssets.evpack build/unity-preview.png build/unity-preview-display.frag.spv
```

Windows 构建命令同样通过 `cmake/with-msvc.cmd` 执行；环境变量
`EVENGINE_VULKAN_VALIDATION=1` 开启验证层。探针最多排列 96 个静态 Prefab，
显示转换在 GPU 上完成，PNG 来自引擎离屏画面；它不是原始 Unity demo 场景的复刻。

兼容该样本的 pathname 精确 `\n00` 尾标记、包级 `.icon.png` 缩略图，以及
`packagemanagermanifest` 中的 `Packages/manifest.json`。依赖清单只保留为源数据，
不安装或执行其中的依赖；普通 GUID 资产的路径仍限制在 Assets 下。
负数的非层级组件 ID 保留在未支持报告中，不再阻断层级导入；负数的
GameObject/Transform ID 仍明确报告不支持 scene-template/1 转换。
