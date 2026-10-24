# Modern Balcony / 现代阳台

以玻璃栏杆、浅色大砖与绿植阳台参考图为基础的现代露台示例。固定建筑和家具主要由 OpenSCAD 建模，植物在启动时程序化生成，两只在线取得的低多边形猫分别播放骨骼动画。运行时不联网，也不需要 OpenSCAD。

## 运行

需要包含 `model3d`、`animation`、`daynight`、`procgen`、`graphics_raytracing`、`ui`、`keyboard` 及其依赖的 Vulkan 引擎构建。标准 `runtime-3d` / `3d` / `full` 配置可提供这些模块。大气天空还要求构建时的 `EVENGINE_SHADER_ASSET_VALIDATION=ON` 能找到 SPIRV-Tools 与 spirv-cross-core（Linux 通常需 `spirv-tools`、`libspirv-cross-c-shared-dev`；Windows 需 Vulkan SDK 对应库）。缺少验证库时会明确报告天空准备失败。根目录执行：

```sh
make run/linux-debug GAME=examples/modern-balcony
```

或在本目录执行 `../../build/linux-debug/src/engine/eve run`。

- 左右方向键：环绕相机；`R`：复位。
- 空格：暂停 / 继续猫和植物动画。
- `F`：开关反射。界面显示 KHR 光追接口是否可用。

## 场景与渲染

- `scad/balcony.scad`：毫米 / Z-up 参数化建筑，包含地坪、砖缝、栏杆及玻璃、木格栅、边缘花槽、沙发与椅架、茶几和陶盆。不同材质分别导出。
- `assets/architecture/*.obj`：SCAD 编译产物，转换为米 / Y-up，分组应用 PBR 材质；并非脚本重新搭建建筑的占位模型。
- `sky.json`：引擎大气天空，Rayleigh / Mie 散射、多重散射、太阳与高度雾。采用程序化大气天空背景，无外部天空贴图依赖。
- CSM 太阳阴影、PBR 金属与玻璃、高质量后处理、filmic 色调映射、bloom 与局部暖光。
- 支持 KHR 光追的设备启用 `rtx`；不支持时使用引擎 `reflectionChain`，界面明确区分。软件 Vulkan 验证不能证明硬件光追效果。
- 有固定种子的程序化树木、紫叶及绿叶盆栽，轻微风摆；茶杯、书、靠垫、地毯、浇水壶、猫碗与灯串。
- 两款独立猫模型（2,636 与 6,376 三角面），独立 skeleton / clip / player / skin。保留模型、动画与材质所有者，逐帧计算姿态并上传蒙皮。

## 重新生成建筑

```sh
python3 scad/build.py --openscad /path/to/openscad
```

该命令使用真正的 OpenSCAD CSG 编译器，`--hardwarnings` 拒绝警告；脚本只转换二进制 STL 的轴向、单位与 OBJ 格式。STL 不保留 SCAD 颜色，因此运行时按材料组赋材质。程序会因编译错误终止，不会静默使用其他几何。

## 动物资源与许可

详见 [CREDITS.md](CREDITS.md) 及 `assets/cats/*LICENSE.md`。两款资源均为 CC BY 4.0，保留来源许可；Tuxedo glTF 中一个重名的无关灯节点被赋予唯一名称，几何与动画数据不变。示例在运行时调整显示比例、位置与标准 PBR 材质。许可证不由引擎仓库的代码许可证替代。

## 本次验证

OpenSCAD 七组几何编译、示例目录检查、引擎构建及真实帧渲染通过。当前截图来自 Mesa 软件 Vulkan；其 KHR 光追接口可用不代表实际存在硬件 GPU。天空已挂载，两款猫的独立蒙皮动画已运行。预览见 [preview/balcony-corrected.png](preview/balcony-corrected.png)。界面区分 KHR 光追接口能力与便携反射，不把软件 Vulkan 误称为硬件 GPU。

## 背景修正

花园有连续地面（Y=-3m）和承托阳台的建筑基座；所有背景树根都锚定到同一地面高度。三种橡树／白杨由 EZ-Tree 的分枝算法生成，使用独立树皮和透明叶簇材质，不再使用球形树冠。源码、固定种子和 glTF 导出器在 `garden/`，运行时不需要 Node。近景植物仍由引擎动态生成。天空采用大气散射底层及独立体积云，相机降低以保留可见地平线。

## 当前视觉限制

当前示例仍偏风格化，体积云边缘偏硬，直射光与植被亮度仍需继续调校。软件 Vulkan 截图用于验证场景构成和渲染路径，不代表硬件 GPU 的画质或性能结论。
