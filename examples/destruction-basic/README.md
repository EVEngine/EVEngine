# Destruction Basic — welded boxes + strain field

最小 Chaos 风格破碎烟雾：两块盒子经连接图焊接，应变场超过阈值后断边并
变成独立动态刚体。

## 运行

```sh
make run/<platform>-debug GAME=examples/destruction-basic
```

Linux headless：

```sh
cd examples/destruction-basic && \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ALSOFT_DRIVERS=null \
  XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  xvfb-run -a ../../build/linux-debug/src/engine/eve run
```

## 操作

- 自动：启动后对焊接中点施加 Strain 场，下一仿真步断边。
- `R`：重置场景。

## 相关

- 设计：`docs/dev/2026-09-29-chaos-destruction-geometry-collection设计.md`
- 模块：`eve.Destruction()`（`physics_destruction`）
