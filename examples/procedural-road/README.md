# Procedural Road Lab

纯数学生成道路（无预制道路/桥梁模型）。先用 **4 个简单场景** 逐个验证几何，再打开复杂立交：

| 键 | scene | 内容 |
|----|-------|------|
| 1 | `straight` | 平坦直线段（无路口） |
| 2 | `curve` | 水平弯道（无路口） |
| 3 | `bridge` | 等高高架直线 + 桥墩（暂不含坡道） |
| 4 | `cross` | 地面十字路口（圆盘路口，无穿模扇形） |

```sh
make run GAME=examples/procedural-road
# headless：用 scene.txt 指定场景
echo straight > examples/procedural-road/scene.txt
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh procedural-road
```

配方参数：`scene=straight|curve|bridge|cross|interchange`，另有 `mesh.roadNetwork` / `tex.roadMarkings`。
设计说明见 `docs/dev/程序化道路系统设计.md`。
