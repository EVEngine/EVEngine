# IK Spider 3D — 自主爬行蜘蛛

2D 版 [`ik-spider`](../ik-spider/README.md) 的三维对应：用 `eve.IK()` 的
`Skeleton3D` + `Solver3D` FABRIK，在 XZ 地面上驱动一只 **8 足蜘蛛** 自主游荡。
每条腿经 `solveChain(hip, foot)` 求解，并用 **极向量（pole）** 把膝盖拉成上拱姿态。

```sh
make run/linux-debug GAME=examples/ik-spider-3d
# Windows: make run/win32-debug GAME=examples/ik-spider-3d
# SDK:     eve run examples/ik-spider-3d
```

## 做法

1. **骨架**：根骨为身体；8 条腿各 3 节（hip → knee → foot）。建完后用 `getChild` 取 id。
2. **约束**：髋 / 膝 / 踝设 yaw+pitch 铰链限，避免腿翻折进身体。
3. **步态**：偶数组与奇数组交替迈步（tetrapod）；抬步时足尖沿正弦弧离开地面。
4. **IK**：每帧写根位移与朝向，种子径向 rest pose，再对每条腿
   `setPole(mid, up)` → `addTarget(foot)` → `solveChain(hip, foot)`。
5. **表现**：腿用 `PrimitivePolyline3D`（world 线宽）实时 `setPolyline`；关节 / 躯体 /
   航点用 `PrimitiveSphere3D` + `setTransform`；相机缓缓环绕。

未新增引擎公共 API。同 seed 下航点序列可复现。

## 操作

| 输入 | 作用 |
| --- | --- |
| Space | 暂停 / 继续 |
| R | 按固定 seed 重建蜘蛛 |

启动打印 `IK_SPIDER3D_READY …`；爬行约 2.8s 后写出 `ik-spider-3d.png`。

## 文件

| 路径 | 说明 |
| --- | --- |
| `main.nut` | 相机、输入、截图 |
| `spider3d.nut` | 建骨、步态、FABRIK+pole、Primitive 同步 |
| `config.nut` | 1280×720 窗口 |

## 验证

```sh
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh ik-spider-3d
```

引擎读回预览（约 2.8s 爬行后自动写出）：

![3D 自主爬行的 IK 蜘蛛](ik-spider-3d.png)
