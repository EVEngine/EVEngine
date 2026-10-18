# RX-78-2 造型参考

本版根据用户明确的「高达那种」方向，参考万代官方 RG RX-78-2 Gundam Ver.2.0 的造型，
重新制作 SCAD / GLB 机甲。不是上一版工业机器人的换色版本。

参考页面：https://en.gundam-official.com/feature/rg/rx-78-2_ver2/

- 正面图：https://en.gundam-official.com/feature/rg/rx-78-2_ver2/images/_images240507/sec05-1_img01.png
- 背面图：https://en.gundam-official.com/feature/rg/rx-78-2_ver2/images/_images240507/sec05-1_img02.png
- 头部结构图：https://en.gundam-official.com/feature/rg/rx-78-2_ver2/images/_images240507/sec05-3_img01.jpg

对照重建了这些特征：双眼、V 字天线、额头摄像机、分体面罩、红下巴、头侧排气孔，
蓝胸甲与黄色双散热口、红色腹部、前后分片裙甲、白色分层肩甲与大腿/小腿、圆形关节，
红色脚掌、白框红色十字盾、独立手持步枪、双背部喷口与光束军刀柄。

保持模型可重建、可绑定和可运行，而不是宣称官方模型的精确复刻。参考图只用于查看与建模
对照，没有作为模型贴图打包；材质仍是本例生成的 PBR 通道与局部磨损。

几何和关节标记的来源为 `assets/ironwarden/design.py`。生成器产出 SCAD 和 `scene.json`；
绑定工具读取同一份零件几何及显式骨骼归属，导出 GLB。

新模型工作室渲染：`evidence/showcase.png`、`showcase-back.png`。
引擎动作证据：`evidence/idle.png`、`walk.png`、`dance.png`。
