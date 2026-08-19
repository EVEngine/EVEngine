# 统一资源缓存（ResourceManager 复活）

> 状态：已实施；日期：2026-08-20
> 目标：把 `common/Resource.h` 里所有分支都被注释掉的 `ResourceManager` 空壳，做成以
> `IAssetReloader` 能力为底座、覆盖 image / font / model3d / sound 的统一 CPU 资源缓存，
> 并接入热重载与依赖级联。
> 涉及改动：`src/engine/common/Resource.h/.cpp`、`src/engine/common/AssetReloader.h`、
> `src/modules/{image,font,model3d,sound}/*Loader.*`、各模块工厂、`graphics` 纹理解码路径、
> `sceneloader` / `housegen` 所有权迁移、`test/resource.cpp`。

## 1. 背景

改造前 `ResourceManager::get()` 的所有创建分支都是注释掉的，永远返回 `nullptr`，全仓库零调用；
`Resource` 只有 `uri` + `dependencies` 两个字段，`dependencies` 也没有消费方。真实的 CPU 资源
（ImageData / FontData / ModelData / SoundData）由各模块工厂各自 `read + decode`，没有任何统一缓存：

- `Font::newFontDataFromFile` / `Model3D::newModelDataFromFile` 每次调用都重新读文件、重新解码；
- 唯一的例外是 graphics 的 GPU 纹理缓存（`texturesByPath`），但 `Texture` 是 `Drawable`，不走
  Resource 体系；
- `IAssetReloader` 只有 reload 半边（graphics / particles / map 各自注册，`HotReload` 只管分发），
  没有 load 半边，无法支撑"缓存 miss 时创建资源"。

这正是架构文档 P8 记录的落点：`ResourceManager` 应复活为"URI → 缓存条目 + 重载器"的注册中心，
而不是一个 `if/else` 分发器。

## 2. 设计

### 2.1 接口：`IAssetReloader` 补齐 load 半边

`common/AssetReloader.h` 的 `IAssetReloader` 增加：

```cpp
enum Priority { kCache = 0, kTexture = 10, kConsumer = 20 };

/** 为 key（规范化 VFS 路径，可带 ?param）创建 CPU 资源；不认领时返回 nullptr。 */
virtual Resource* load(const std::string& key) { return nullptr; }

/** 默认 no-op；世界对象级重载（particles/map）继续各自实现。 */
virtual bool reload(const std::string& normPath) { return false; }
```

四个资源模块各自注册一个 provider（`*Loader.cpp`，静态注册在 `kCache` 优先级），`ResourceManager`
在缓存 miss 时用 `cap::forEachUntil` 找到第一个"认领 + load 成功"的 provider。`HotReload` 分发逻辑
不变：文件变化时它照旧遍历监听器，只是现在排在 `kCache` 的"缓存刷新器"会先跑。

### 2.2 缓存键

- 键 = 规范化 VFS 路径（`\` → `/`、去 `./`、去尾部 `/`），可选 `?param=value`；
  如 `fonts/a.ttf?size=16`。
- 参数参与键的构造：同一个字体文件在 16px 和 24px 是两个条目；模型解码选项（5 个 bool）序列化进
  查询串，`Model3D::newModelDataFromFile(path, options)` 与 `ModelLoader` 共用
  `modelCacheKey` / `modelOptionsFromKey`。
- `ResourceManager::pathOfKey` 剥离参数，供 provider 做扩展名匹配、供热重载按"裸路径"命中所有参数
  变体。

### 2.3 身份稳定：`Resource::adopt`

缓存条目持有 `ref<Resource>`，跨重载保持**同一个实例**：文件变化时 provider 重新 `load()` 出一个
新实例，`ResourceManager::refreshEntry` 调用 `cached->adopt(fresh)` 把内容搬进旧实例（各子类用
`std::swap` 搬走 payload），再销毁被抽干的临时实例。这样所有既有持有者（裸指针、`eve::ref`、脚本
实例）在热重载后仍指向有效对象，不需要 `Object::setUpdate` 的指针跳转机制，也不会悬垂。

四个子类的 `adopt`：

| 资源 | 搬移内容 |
| --- | --- |
| `ImageData` | 像素缓冲区、宽高、格式、解码器句柄、像素函数 |
| `FontData` | 字体字节、FT_Face、字号、字形缓存 |
| `ModelData` | `medialoader::ModelScene`（move-only，swap 即搬移） |
| `SoundData` | PCM 缓冲与格式元数据 |

### 2.4 热重载接线

`ResourceManager` 自己实现 `IAssetReloader` 并**惰性注册**为第一个监听器（`kCache` 优先级，
`getInstance()` 首次调用时注册）：

- `handlesPath(path)`：缓存里存在路径匹配的条目才认领；
- `reload(path)`：把该路径所有参数变体的条目重新 load + adopt，并对"把该条目列为依赖"的其他条目
  递归刷新（`visited` 集合防环）。

这样 `filesystem/HotReload.cpp` 一行未改：它把文件变化分发给监听器，缓存刷新器自然排在 GPU
纹理重传（`kTexture`）和消费者重绑定（`kConsumer`）之前。

### 2.5 线程安全

`ResourceManager` 内部用 `std::mutex` 保护缓存 map；provider 的 `load()`（读文件 + 解码）在锁外
执行。这同时服务了 `sceneloader` 的异步预加载：后台线程池 decode 与主线程读取可以并发插入条目。
重载（`adopt`）预期在主线程的热重载分发里发生。

## 3. 模块接入

| 模块 | 新文件 | 工厂改动 |
| --- | --- | --- |
| image | `ImageLoader.cpp` | 新增 `Image::newImageDataFromFile(path)`；graphics 两个后端的
  `newTextureFromFile` / `reloadTextureFromFile` 改走缓存，一次路径只解码一次 |
| font | `FontLoader.cpp` | `Font::newFontDataFromFile(path, size)` 改走缓存（key 带 size） |
| model3d | `ModelLoader.h/.cpp` | `Model3D::newModelDataFromFile(path[, options])` 改走缓存 |
| sound | `SoundLoader.cpp` | 新增 `Sound::newSoundDataFromFile(path)` |

所有权迁移：`sceneloader` 删除对 `ModelData` 的 `delete`（缓存持有）；`housegen` 的
`unique_ptr<ModelData>` 换成 `eve::ref<ModelData>`；相关测试同步迁移。

## 4. 测试

- `test/resource.cpp`（新增，纯 common，不依赖模块）：键规范化、命中/未命中、参数分桶、
  unload / unloadPath、原地重载（身份稳定 + 内容刷新）、依赖级联、依赖环安全、无缓存条目时
  reload 为 no-op。
- `test/font.cpp`：`newFontDataFromFile` 同 (path,size) 共享实例、不同 size 分桶。
- `test/model3d.cpp`：`newModelDataFromFile` 同路径共享实例、不同 options 分桶。

## 5. 后续

- `res://` 生成资源：`dependencies` 机制已就绪，等出现真正的生成资源类型后，provider 为其注册
  load 即可获得"依赖文件变化 → 级联刷新"。
- `Texture` 纳入 Resource 体系后，GPU 纹理缓存可以并入同一套键空间，替换 `texturesByPath`。
