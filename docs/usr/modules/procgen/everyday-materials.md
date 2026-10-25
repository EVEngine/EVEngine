# 日常材质生成

`EverydayMaterials` 提供 wood、metal、rubber、cloth、plastic、steel、glass、paper、
aluminum、castIron、cement、paint、marble 专用入口。每个入口返回
`Result<std::unique_ptr<PbrTextureSet>>`，底层统一经过图案 → 色素/浮雕 → 六张 PBR 图。
木纹和织物具有方向结构，金属采用拉丝，纸采用纤维，生铁与水泥采用颗粒，
大理石采用扭曲色脉。金属、钢、铝和生铁使用金属度 1，其余为 0。
这是材质表面的程序化近似，不是实测光学数据库，也不负责物体几何。

```cpp
#include "procgen/texture/EverydayMaterials.h"
eve::procgen::Params p;
p.setSize(512, 512);
p.setSeed(42);
p.setFloat("finish", 0.8f);
auto result = eve::procgen::EverydayMaterials::wood(p);
if (!result.ok()) return; // 调用方按自己的错误处理策略处理诊断
const auto maps = std::move(result).takeValue();
// maps 独占六张 CPU 图片；上传沿用现有流程，离开作用域自动释放。
```

| 参数 | 默认 | 语义 |
| --- | --- | --- |
| size / seed | Params 默认值 | 每边 1–2048；setSize 将非正值归一为 1；超上限返回 InvalidArgument |
| scale | 4 | 1–32 个重复，取整；与输出分辨率独立，低分辨率使用预过滤 |
| detail | 0.5 | 0–1，微表面起伏强度 |
| finish | 0.5 | 0–1，越高越光滑，保留材质固有粗糙度差异 |
| variation | 0.4 | 0–1，色素变化幅度；不改变结构和法线 |
| tintR / tintG / tintB | 各材质本色 | 0–1，每个通道独立覆盖 |

数值越界会截断；非有限数、错误类型使用默认值；未知参数不影响生成。
图案在单位 UV 平面周期连续；首尾像素是相邻采样点，不要求数值相等。
seed 和有效参数相同，在同一工具链产生相同字节；跨平台浮点噪声不保证逐字节一致。
无全局随机数、时间、ECS、Link、事件或持久化格式。工厂不保存输入，可在不同输入上并发调用。

脚本和编辑器通过现有 Result/owned-proxy 入口使用 `pbr.everyday.wood` 等配方，
生铁 id 为 `pbr.everyday.cast_iron`，其余后缀与上面的英文名一致。
原有 `pbr.wood` 等配方保持其已有风格，避免改变已制作场景。
schema 提供尺寸、种子和常用参数；tint 通道可通过 Params 手动设置。
生成、释放和 stale 检查沿用 `generatePbrMaterial` / owned proxy 生命周期。

玻璃输出低起伏、非金属表面，C++ 输出同时携带 `transmission=0.96`、`ior=1.5`。
alpha 仍表示覆盖率。现有默认渲染器不会自动启用折射/透射，脚本可通过 `getTransmission()` / `getIor()` 读取
这两个光学标量；需要透射的消费者应接入自己的透射 shader。
仅应用六张贴图不等于已经实现透明玻璃渲染。

昂贵步骤仅生成 CPU 贴图，不隐式上传 GPU。共享管线独立处理 pigment 与 height，
因此大理石色脉不会被放大为深沟。PBR 贴图槽的现有限制见模块主文档。

共享 AO 改为可分离滑动均值，像素数相同时不再随采样半径平方增长。
与原邻域参考算法的 RGBA8 结果允许最多 1 级量化误差；旧配方的参数和图案不变。
