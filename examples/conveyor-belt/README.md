# 传送带：UV 滚动 + 物理切向速度

运行：`make run/linux-debug GAME=examples/conveyor-belt`

皮带几何不动。视觉上用 `Material.setAlbedoUvOffset` 滚动条纹贴图；
物理上用 `Shape3D.setTangentVelocity` 把箱子沿 +X 送走。两者共用同一速度 `V`：

```
d(offsetU)/dt = -V * tilesU / beltLengthX
```

（`newMeshCube` 顶面 U 沿局部 +X；正速度时条纹与货物同向，故 offset 取负号。）

按键：`[` / `]` 调速，`R` 复位箱子，`Space` 暂停。
