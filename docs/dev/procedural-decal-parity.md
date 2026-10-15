# Procedural Decal 功能对照与 EVEngine 实现

参考范围：用户提供的参考资产包。证据来自包内 550 个 `.uasset`、40 个正式
`.sbsprs` Substance 预设、材质函数、Render Material 和示例资产。Unreal `.uasset` 是专有
容器，不作为可移植源码；EVEngine 导入 `.sbsprs` 参数并用原生确定性 baker 生成纹理。

## 核算方法

按可独立验证的输入、输出、作者参数、导入行为和运行时行为拆分为 46 项。只有端到端实现并
有测试证据的项目计为完整；部分实现不计分。当前 **42/46 = 91.3%**。

| # | 参考能力 | EVEngine 证据 | 状态 |
| ---: | --- | --- | --- |
| 1 | Base Color 输出 | `ProceduralDecalBake::albedo.rgb` | 完整 |
| 2 | Alpha 输出 | `albedo.a` 与 coverage | 完整 |
| 3 | Normal 输出 | 高度梯度生成 `normal` | 完整 |
| 4 | Roughness 输出 | `params.r` | 完整 |
| 5 | Metallic 输出 | `params.g` | 完整 |
| 6 | Emissive 输出 | 逐层 emissive，写入 `params.b` | 完整 |
| 7 | Height 输出 | `params.a` | 完整 |
| 8 | Channel packing | RGBA8 params 合同 | 完整 |
| 9 | Layer A | `ProceduralDecalLayer layerA` | 完整 |
| 10 | Layer B | `ProceduralDecalLayer layerB` | 完整 |
| 11 | Layer amount | `amount` | 完整 |
| 12 | Layer scale | `scale` | 完整 |
| 13 | Layer rotation | `rotation` | 完整 |
| 14 | Layer blur | 有界 separable blur | 完整 |
| 15 | Layer contrast/tweak | `contrast` 与导入映射 | 完整 |
| 16 | Layer color | 逐层 RGB | 完整 |
| 17 | Color variation | 确定性逐像素变化 | 完整 |
| 18 | Roughness variation | 逐层变化 | 完整 |
| 19 | Normal intensity | `normalStrength` | 完整 |
| 20 | Per-layer metallic | 逐层插值 | 完整 |
| 21 | Per-layer emissive | 逐层插值 | 完整 |
| 22 | Per-layer height | 逐层高度 | 完整 |
| 23 | AB height masking | `layerHeightBlend` | 完整 |
| 24 | Global opacity | `recipe.opacity` | 完整 |
| 25 | Random seed/reproducibility | 相同配方字节一致 | 完整 |
| 26 | `.sbsprs` admission | `importProceduralDecalSbsprs` | 完整 |
| 27 | Reference material preset import | 40/40 正式预设夹具逐个解析、烘焙 | 完整 |
| 28 | Native preset families | 10 个稳定 EVEngine 家族预设 | 完整 |
| 29 | Imported preset GPU upload | `bakeSbsprsTexture` | 完整 |
| 30 | EveScript authoring workflow | preset/import bake、texture attach、runtime setters | 完整 |
| 31 | Planar UV | Vulkan/WebGPU | 完整 |
| 32 | Local triplanar UV | Vulkan/WebGPU | 完整 |
| 33 | Spherical UV | 经纬 UV、接缝 wrap | 完整 |
| 34 | World-aligned UV | 世界坐标锚定三平面 UV | 完整 |
| 35 | POM | 高度 ray marching、视角自适应 1..64 层 | 完整 |
| 36 | Backface cull | 四种投影保留背面剔除 | 完整 |
| 37 | Edge mask/feather | `setEdgeFade`，宽度 0..0.49 | 完整 |
| 38 | Projection depth volume | box depth reconstruction/cull | 完整 |
| 39 | Atlas UV | `uvRect` | 完整 |
| 40 | Surface/blend modes | over/add 与 normal/roughness/metal/emissive 强度 | 完整 |
| 41 | Runtime management | lifetime、fade、quota、batch、frustum/distance cull | 完整 |
| 42 | Editor/persistence | 属性、原子发布、Undo/Redo、gizmo、snapshot v4；迁移 v1-v3 | 完整 |
| 43 | Baker 内任意 Custom Input 图片节点 | 运行时可传外部纹理，但 baker 不继续加工输入图 | 部分，不计 |
| 44 | Substance 完整 pattern 目录 | source pattern id 确定性映射到 5 个原生 pattern 家族 | 部分，不计 |
| 45 | Layer Independent / Combine Mask 图变体 | 固定双层高度混合，没有独立图输出拓扑 | 未实现 |
| 46 | 可配置 Angle Fade / Depth Fade | 有背面剔除和硬深度体，但没有独立连续参数 | 部分，不计 |

## 导入资产与测试证据

- `test/fixtures/procedural_decal/*.sbsprs` 是参考 ZIP 内全部 40 个正式预设，覆盖 Blood、Damage、
  Dirt、Lichen、Metal、Mold、Moss、Paint、Puddle、Rust。非正式 `Test.sbsprs` 不计入。
- `decal.proceduralImportsReferenceSubstanceMaterialPresets` 自动枚举 40 个文件，逐一解析并以
  32×32 烘焙 albedo/normal/params。
- `decal.proceduralFacadeUploadsAllRuntimeChannels` 同时验证内置预设和导入预设的真实 GPU 上传。
- `decal.gpuTriplanarCoversGrazingWall` 对 planar、local triplanar 和 world-aligned 进行 Vulkan
  像素差分；`decal.renderGalleryPng` 覆盖 spherical 与 POM 的真实 Vulkan 渲染。

## 数据与运行时契约

- `ProceduralDecalRecipe::schemaVersion` 当前为 2；v2 保留参考 pattern ID、tile 与 mask invert；
  未知版本返回结构化失败。
- `.sbsprs` 导入器忽略未知参数以允许前向扩展，但已知参数格式错误会拒绝整个输入，不发布
  部分配方。
- 配方和 bake 输出由调用者拥有。baker 不访问 GPU、wall clock 或全局缓存，可在线程中运行。
- `params` 固定为 roughness / metallic / emissive / height；POM 读取 alpha。
- 烘焙尺寸限制为 1..4096；成本随像素和 blur 半径增长，只允许在 admission、编辑或加载阶段。
- 编辑器 decal snapshot 当前为 v4；v1 补 POM 和 edge fade 默认，v2/v3 补 edge fade，未知版本拒绝。

## 剩余 8.7%

后续若要继续逼近完整作者工具，应按顺序实现：Custom Input 图像采样；完整 pattern/curve 目录；
Layer Independent 与 Combine Mask 输出图；独立的 Angle Fade 和 Depth Fade 参数。商业纹理像素和
Unreal `.uasset` 容器不需要进入 EVEngine，`.sbsprs` 是当前受支持的可移植作者资产边界。
