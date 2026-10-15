# SCAD 与 LDraw 模型导入

[资源管线](asset.md) · [English](../../en/usr/modules/asset-structured-models.md)

这两个入口属于离线资产导入：源模型 → `.eva` → `cook` → `.evpack` → 原生静态 prefab。
游戏运行时不执行 OpenSCAD，也不需要 LDraw 零件库。当前不是
`Model3D.load("model.scad")` / `Model3D.load("model.ldr")` 的直接脚本加载接口。

## SCAD：用完整 OpenSCAD 编译器生成几何

从 [OpenSCAD 官网](https://openscad.org/downloads.html)安装桌面编译器，或准备解压的便携版本。
支持能力由所用 OpenSCAD 版本决定，包括其支持的参数、循环、模块、CSG、include/use 和 import。
引擎通过参数数组启动编译器，不经过 shell；超时、非零退出、缺失输出和坏网格均失败，
失败时不发布 `.eva`。编译使用 `--hardwarnings`，缺失 include 等警告也会停止导入。
`--scad-timeout` 默认 120 秒，范围 1–3600 秒。

```sh
eve asset import test/fixtures/structured_models/cup.scad --from scad \
  --openscad /absolute/path/to/openscad --out cup.eva \
  --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31040 --name demo.cup
eve asset validate cup.eva
eve asset cook cup.eva --target windows-x86_64-vulkan --out cup.evpack
```

Windows 上把可执行文件路径替换为 `C:/Program Files/OpenSCAD/openscad.exe`，
带空格的路径必须加引号。示例源文件是原创杯子，带布尔挖空与把手。

编译结果使用普通 binary STL；按毫米、Z-up 转换为米、Y-up，保留三角形绕序并重建面法线。
输出 `eve.mesh/2`、默认中性 `eve.material/15` 和 `eve.scene-template/3`。
STL 不带颜色、UV、骨骼或动画，因此 SCAD 的 `color()`、ScadRig 等信息不会转换为运行时数据。
源包保留根 `.scad` 文本；依赖文件闭包不自动归档，重新编译需要继续保留原始依赖。
OpenSCAD 正常读取 include/use/import 文件；这是本地创作工具，不是执行沙箱。

## LDraw：原生解析零件组合

无需外部转换器。接受 `.ldr`、`.dat` 和包含 `0 FILE` / `0 NOFILE` 的 `.mpd`。
命令读取输入所在目录的 LDraw 文本文件，额外零件库用 `--ldraw-library` 指定：

```sh
eve asset import test/fixtures/structured_models/blocks.mpd --from ldraw \
  --out blocks.eva --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31041 --name demo.blocks
eve asset validate blocks.eva
eve asset cook blocks.eva --target windows-x86_64-vulkan --out blocks.evpack

eve asset import my-model/model.ldr --from ldraw --ldraw-library /absolute/path/to/ldraw \
  --out model.eva --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31042
```

零件库目录应包含 `parts/`、`p/`，以及可选的 `LDConfig.ldr`。不自动下载第三方零件。
库的许可与再分发权限仍由项目作者确认。测试 MPD 自带原创盒子零件，不依赖第三方库。

- 类型 1：递归展开零件引用，应用平移与 3×3 矩阵；支持相对目录、`parts/`、`p/`、`models/` 搜索，
  文件名 ASCII 大小写不敏感、兼容反斜杠。循环、缺失引用、奇异变换、路径穿越和歧义源名失败。
- 类型 3 / 4：三角形及四边形转换为三角网格；BFC CW/CCW 和 INVERTNEXT 控制绕序。
- 颜色 16 继承父零件颜色；根默认颜色 7。基础 0–15 颜色内置；其他编码从 `!COLOUR`
  （通常在 LDConfig）读取，支持直接 RGB 颜色。sRGB 转换为线性 PBR baseColor，保留 alpha。
  局部 `!COLOUR` 从声明处生效，传入子文件，在返回父文件后恢复；其他未引用文件与
  `LDCfgalt.ldr` 不会覆盖默认调色板。
- 1 LDU = 0.4 mm，源坐标 `(x,y,z)` 转成米制 `(x,-y,-z)`。
- 同颜色几何合并为一个 mesh/material，静态 prefab 的每个 renderer 指向相应资产。
  零件原始文本及使用到的颜色定义保留在源包中，变换已烘焙；不保留可编辑零件实例树。
- 类型 2 / 5 边线、特殊材质 finish/luminance 只保留源文本并在报告标注；
  TEXMAP、嵌入数据和未知几何类型明确失败。尚不生成 LEGO 连接关系、碰撞或搭建规则。

## 运行时与重新导入

`.eva` 的 `default` entrypoint 是静态场景 prefab，`mesh:<part>` 是单独网格。
用现有原生 `EvpackResourceReader` / `EvpackStaticPrefab::load` 消费；
API 的参数与所有权契约见 Doxygen，当前没有通用 Asset 脚本加载器。
运行时 reader 和 GPU factory 的生命周期按现有资产桥接契约管理。

相同 package UUID 和相同颜色/part key 生成稳定资产身份；复用 UUID 重新导入，
再 cook 和发布。导入器本身不修改运行中的 ECS、GPU 对象或编辑历史。
CPU 解析与编码对相同输入确定；OpenSCAD 编译结果取决于编译器版本和源依赖，
不要把跨版本二进制一致性当作保证。源包报告 `reports/import.json` 记录转换与限制。

契约与组合测试：[asset_import_structured_models.cpp](../../../test/integration/asset-pipeline/asset_import_structured_models.cpp)。
