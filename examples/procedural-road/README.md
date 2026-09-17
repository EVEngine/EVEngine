# Procedural Road Lab

纯数学生成的多层立交道路（无预制道路/桥梁模型）：

- 截面挤出：沥青 / 路缘 / 人行道 / 桥面板
- 高程桥墩：超过 `pierClearance` 时按间距放置
- 逻辑标线：边线、虚线、斑马线、导向箭头（几何 + `tex.roadMarkings`）
- 导航叠加：车道中心线与路口转弯弧（`nav` group）

```sh
make run GAME=examples/procedural-road
# or headless:
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh procedural-road
```

配方：`mesh.roadNetwork`、`tex.roadMarkings`。设计说明见 `docs/dev/程序化道路系统设计.md`。
