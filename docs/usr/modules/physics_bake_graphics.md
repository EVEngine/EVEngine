# 烘焙呈现适配

physics_bake_graphics 是唯一保留的 bake 跨域模块，依赖 graphics 和 physics_backend，
不依赖求解器。原生和脚本呈现合并在 physics/bake_graphics/。

原生呈现器拥有冻结资产和 CPU 缓冲，不拥有时钟。createMesh(player,graphics)、
upload(player,graphics,mesh)、draw(player,graphics,material) 同步借用播放器。
不同冻结资产的播放器会在 GPU 写入或提交前被拒绝。

脚本 prepareDeform(player,radius)/prepareParticles(player,ids) 只接受 VM 拥有的播放器，
并保留其拥有引用。控制与绘制共享传入对象的时间、速率、暂停和 placement。
原生借用包装器必须先调用并检查 prepareInstance()，取得 VM 拥有的独立播放器。
需要独立实例同样使用 prepareInstance()，呈现适配不会隐式创建第二份状态。
材质、纹理、shader、相机、canvas 按 draw 借用，必须存活至 Graphics 完成相关帧。

见 [Physics 烘焙与重放](physics.md#烘焙与重放)。
