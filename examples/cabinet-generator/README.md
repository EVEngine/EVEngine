# 柜子自动生成器

运行：`make run/linux-debug GAME=examples/cabinet-generator`。

按 1/2/3/4 切换欧式/中式/简约/现代；D 循环 0–4 扇门，R 循环 0–4 个抽屉，
S 循环 1–5 层，L 切换柜脚，A/F 转动查看。宽高深、分柜数和板厚可在 main.nut 中修改，热重载更新。

使用现有 `procgen.buildMesh("mesh.cabinet", params)`；完整参数及边界见
[接口说明](../../docs/usr/modules/procgen/cabinet.md)。这是程序化家具几何；门抽屉保留独立分组，示例不包含开关动画。


柜体、门/抽屉面板、内板、饰条、把手与柜脚按材质角色分别绑定贴图。
欧式用暖橡木和黄铜，中式用深色木和古铜色把手，简约用白漆和浅木脚，现代用蓝灰漆和拉丝钢。
在 main.nut 的 cabinetStyleMaterials() 中逐部位修改配色、母材质和金属度。
素材说明见 [materials/README.md](materials/README.md)。

场景包含棚拍环境反射、主光/补光和接收阴影的地面，避免金属在无反射环境下发黑。

[查看实际渲染参考与验证范围](screenshots/README.md)。
