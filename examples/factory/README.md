# Factory — 流水线垂直切片

演示 `logistics` 模块：矿石源 → 传送带 → 冶炼机 → 传送带 → 汇，
并用 `gpgpu` compute 把物品 sim floats 解成世界坐标后绘制。

## 运行

```bash
make run/<platform>-debug GAME=examples/factory
# Cloud / headless smoke:
# MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh factory
```

## 操作

- `空格`：暂停 / 继续
- `R`：重建产线
- 画面左侧为矿石（橙），右侧产物（青）；灰格是传送带，蓝块是机器

权威模拟在 CPU；GPU 只负责位置解算（`item_pos.comp` → `gpgpu`）。无 Vulkan / `gpgpu` 时自动回退到 CPU 世界坐标打包。
