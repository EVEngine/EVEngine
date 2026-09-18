# Procedural Road Lab

纯数学生成道路（无预制道路/桥梁模型）。简单场景验证几何后，打开复杂立交：

| 键 | scene | 内容 |
|----|-------|------|
| 1 | `straight` | 平坦直线段（无路口） |
| 2 | `curve` | 水平弯道（无路口） |
| 3 | `bridge` | 等高高架直线 + 桥墩 |
| 4 | `cross` | 地面十字路口（四臂外推 + 圆心朝外圆弧转角） |
| 5 | `interchange` | 地面十字 + 高架环 + 对角上跨 + 两条匝道 |

```sh
make run GAME=examples/procedural-road
# headless：用 scene.txt / EVENGINE_ROAD_SCENE 指定场景
echo interchange > examples/procedural-road/scene.txt
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh procedural-road
```

配方参数：`scene=straight|curve|bridge|cross|interchange`，另有 `mesh.roadNetwork` / `tex.roadMarkings`。
设计说明见 `docs/dev/程序化道路系统设计.md`。
