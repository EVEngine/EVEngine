# 烘焙功能归属迁移

`physics_bake_recording_script` 的独立模块已撤销。功能归属已有 Physics/Cloth/Rope/Particles；
资产和采样归属 physics_backend，呈现归属 physics_bake_graphics。
旧脚本 facade 仅兼容，不再注册模块；持久格式保持不变。

见 [Physics 烘焙与重放](physics.md#烘焙与重放)。
