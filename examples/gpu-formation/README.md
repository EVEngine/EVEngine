# GPU Formation — `eve.TF()` 阵型系统

纯脚本示例：用张量包在 GPU（或自动回退 CPU）上做大规模阵型部署，**不改任何 C++**。

展示 TF 包的几个核心能力：

1. **模板生成**：`arange` / `sin` / `cos` / `reshape` / `concat` 搭出圆形等阵型；
2. **齐次矩阵乘法**：局部槽位 `[N,3]` × 领袖变换 `[3,2]` → 世界坐标（上一问里的「阵型坐标系切换」）；
3. **`tf.func()` + `compile()`**：把「matmul 部署 + soft pursuit」编成专用 kernel，每帧一次 `run3`；
4. **阵型 Morph**：两套偏移做广播插值，切换时平滑变形。

## 运行

```bash
make run/<platform>-debug GAME=examples/gpu-formation
```

Linux 无头烟测：

```bash
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh gpu-formation
```

## 操作

| 键 | 作用 |
|---|---|
| `1`–`5` | 阵型：grid / wedge / circle / line / column |
| `WASD` / 方向键 | 移动领袖 |
| `Q` / `E` | 旋转朝向 |
| `Space` | 暂停 |
| `R` | 单位瞬移到当前槽位 |

启动后约 3 秒内会自动绕圈并轮换阵型（方便烟测与录屏）；任意键操作后退出自动演示。

## 相关

- 模块文档：[`docs/usr/modules/tensor.md`](../../docs/usr/modules/tensor.md)
- 更广谱的算子演示：[`examples/tensor`](../tensor/README.md)
