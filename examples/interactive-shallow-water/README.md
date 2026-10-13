# Interactive Shallow Water（GPU 浅水方程 + 透明水面）

**仿真**用 Saint-Venant / 浅水方程在 GPU（`gpgpu` compute）上求解可交互水面：高度 `h`
与水平动量 `(hu, hv)`。人物腿部用 Capsule 碰撞体采样进格子。无 GPU 时回退 CPU。

**表现**走引擎内置透明 `gfx.newWater()` 着色器（折射 / 深浅色 / Fresnel / 岸边泡沫），
仿真网格每帧写入 Water 平面网格。场纹理 RGBA 只作数据图例，不当湖面 albedo。

## 运行

```bash
cd examples/interactive-shallow-water
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/interactive-shallow-water
```

## 操作

| 输入 | 作用 |
|---|---|
| `A` / `D` / `W` / `S` | 移动角色（双腿 capsule 跟随；无输入时会慢速巡逻以显涟漪） |
| 鼠标左键 | 在水面点击处制造冲击波 |
| `Space` | 暂停 / 继续 |
| `R` | 重置水深与角色位置 |
| `G` | 打印当前是否走 GPU 路径 |
| `F` | 显示 / 隐藏场数据图例板 |

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 场景、腿部碰撞、仿真网格 → Water 网格、输入 |
| `config.nut` | 窗口配置 |

## 技术要点

- 仿真：`eve.Fluids().newShallowWater(resX, resZ, cellSize)` → 每帧 `setColliderCapsule` + `step`
- 网格：`buildSurfaceMesh(visualHeightGain)` → `gfx.updateMeshVertices(waterFx.getMesh(), …)`
  （世界坐标换算到 `createPlane` 的居中局部空间）
- 表现：`gfx.newWater()` + `Material.setSurfaceMode("transparent")` + `waterFx.getShader()`
  （`eve.graphics.stylized-water/1` JSON：半透明 opacity、折射、轻量程序波叠加）
- 场纹理 `copyFieldToImage`：**R**=crest heat，**G/B**=vx/vz，**A**=速度；仅贴在图例板上
- 单元测试见 `test/fluids_shallow_water.cpp`
