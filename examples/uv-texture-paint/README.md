# UV 贴图绘制：点击网格把 Box3D 三角形命中映射回模型 UV 并绘制贴图

示例加载 `paintable.obj` 中的平面四边形，并把它同时注册为可渲染模型和 Box3D 静态三角网格碰撞体。
鼠标左键点击时，示例用相机屏幕射线在物理世界中做三角形拾取，把命中的世界空间点通过
`model.mapSurfacePointToUv` 映射回模型 UV，再在 512×256 的 RGBA8 贴图上绘制圆形笔刷并上传给 GPU。
渲染体与碰撞体都是单位变换，所以世界空间命中点即模型本地坐标。

## 运行

```sh
make run/<platform>-debug GAME=examples/uv-texture-paint
```

```sh
cd examples/uv-texture-paint && ../../build/linux-debug/src/engine/eve run
```

Windows 下可执行文件路径为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 相机屏幕射线：`camera.screenToRay(mouse.getX(), mouse.getY(), gfx.getWidth().tofloat(), gfx.getHeight().tofloat())`，再用 `getScreenRayOriginX/Y/Z` 与 `getScreenRayDirX/Y/Z` 取出射线，向方向延长 100.0 作为终点。
- 物理三角形拾取：`physics.newWorld3D(0.0, 0.0, 0.0, false)` 建世界，`world.newBody("static", ...)` 加静态刚体，`body.newTriangleMeshShape(vertices, indices)` 的顶点与索引直接由 `model.getVertexPosition(0, vertex, component)` 和 `model.getFaceVertexIndex(0, triangle, corner)` 展开；`world.rayCast(...)` 后用 `world.hasRayHit()`、`world.getRayHitTriangleIndex()`、`world.getRayHitX/Y/Z` 读取命中结果。
- UV 回映射：`model.mapSurfacePointToUv(0, world.getRayHitTriangleIndex(), hitX, hitY, hitZ, 0)` 返回 `SurfaceUv`，由 `uv.getTriangleIndex()`、`uv.getU()`、`uv.getV()` 给出三角形与 UV 坐标。
- 贴图绘制与上传：`eve.Image().newEmptyImageData(512, 256, "RGBA8")` 建图，逐像素 `pixels.setPixel(...)` 写入水平渐变（r 从 0.12 递增到约 0.30，g 0.28，b 0.55），`gfx.newTexture(pixels, false, false)` 建纹理并由 `renderable.setTexture(texture)` 绑定；命中后 `pixels.paintCircleUv(u, v, 14.0, 1.0, 0.12, 0.04, 1.0, false, false)` 画笔刷，再 `gfx.updateTextureFromImageData(texture, pixels)` 把改动刷到显存。
- 模型来源：`model3d.newModelDataFromFile("paintable.obj")` + `model3d.createRenderable(gfx, model, 0)`；`eve_render` 中 `gfx.clear()` 后 `gfx.render3D()`，背景色为 `gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0)`。

## 操作

- 鼠标左键（`mouse.isDown(1)`）：按下瞬间（上一帧未按下）触发一次 `paintHit()`，在命中 UV 处画半径 14 像素、颜色 (1.0, 0.12, 0.04)、不透明的圆；按住不放不会连续绘制，需要松开后再次点击。
- 鼠标位置（`mouse.getX()` / `mouse.getY()`）决定屏幕射线，从而决定落笔位置。
- 代码中没有键盘绑定，唯一的输入检测就是上述左键。

## 输出

示例不写任何磁盘文件，绘制只发生在内存中的 512×256 RGBA8 `ImageData` 与 `Texture` 上，退出后不保留；
每次绘制会向控制台打印 `paint: triangle=<n> uv=(<u>, <v>)`。

## 相关文件

- `main.nut` — 全部示例逻辑：相机、模型加载、物理世界与三角网格、射线拾取、UV 映射、贴图绘制与上传。
- `config.nut` — 窗口标题 `UV Texture Paint`，窗口尺寸 960×640。
- `paintable.obj` — z=0 平面上的四边形（x∈[-2, 2]、y∈[-1, 1]，法线 +Z）：4 个顶点 `v`、4 个 UV `vt`、1 个法线 `vn`、2 个三角形 `f`。
