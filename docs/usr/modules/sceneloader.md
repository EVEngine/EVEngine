# 场景加载器（SceneLoader）

**脚本入口：** `eve.SceneLoader()`

把 3D 资产（glTF / OBJ / FBX，经 Assimp）解码成 `scene` 声明式场景树并挂载：
每个 `aiNode` 对应一个 `SceneNode`（GameObject），网格节点自动链接
`Renderable3D`（材质/贴图/PBR 参数），光照/相机/动画按选项导入。支持
热重载（diff 只改变化的部分）与异步解码。

## 基本用法

```squirrel
loader <- eve.SceneLoader();
local host = loader.load("models/room.glb");
if (host != null) {
    print("nodes=" + loader.nodeCount("models/room.glb") +
          " lights=" + loader.lightCount("models/room.glb") +
          " cameras=" + loader.cameraCount("models/room.glb") + "\n");
}
```

## 目标导向指南

### 场景热重载

编辑器里改模型/材质后调用 `loader.reloadChecked(path)`：重新解码 → diff →
只增删改变化节点，未变化对象不重传 GPU。返回 true 表示有更新。

### 大场景异步加载

```squirrel
if (loader.pendingAsyncCount() == 0) loader.load("models/big.glb");  // 后台解码
// 主线程每帧轮询：
loader.pollAsync();          // 就绪后挂载（GPU 上传在主线程）
```

## API 快查

### `SceneLoader`（模块）

- `load(path)` → `SceneHost`（null 表示失败）；默认自动链接 Renderable3D。
- `host(path)` → 已挂载的 `SceneHost`。
- `reloadChecked(path)` → bool：热重载并 diff 应用。
- `unload(path)`：卸载并销毁链接的 Renderable3D。
- `nodeCount(path)` / `loaded(path)`。
- 异步：`pollAsync()`、`pendingAsyncCount()`、`prewarm(path)` / `clearPrewarm()` /
  `prewarmed(path)`。
- 内容统计：`lightCount(path)`、`cameraCount(path)`、`animationCount(path)`。

## 生命周期

- 返回的 `SceneHost` 归 scene 模块管理，卸载/热重载后旧句柄失效。
- 异步加载的 GPU 上传必须发生在主线程，所以用 `pollAsync()` 而非在 worker 里挂载。
- 材质贴图按路径缓存并跨网格共享；`unload` 会释放该路径的 GPU 资源。

