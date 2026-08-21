# 按需裁剪模块

EVEngine 默认把全部模块编进引擎。如果你的游戏只用到其中一部分，可以在构建时把其余的裁掉，
减少编译时间、二进制体积和第三方依赖。

> <b>下载的预编译 SDK 默认包含全部模块</b>，无需也不支持在安装后裁剪；本节只对“从源码构建”的用户有效
> （构建方法见根目录 [Readme.md 的“从源码构建”章节](https://github.com/EVEngine/EVEngine/blob/main/Readme.md#从源码构建引擎开发者--贡献者)）。

引擎架构与这套机制的设计依据见[模块编排与裁剪架构](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/模块编排与裁剪架构.md)。

## 预设档位

大多数情况选一个档位就够了：

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEVENGINE_PROFILE=2d
```

| 档位 | 内容 | 适合 |
|------|------|------|
| `minimal` | 窗口、事件、输入、图形、数学、文件系统、UI | 自己搭一切的引擎壳 |
| `2d` | `minimal` + 图片、字体、地图、粒子、动画、物理、音频、空间索引、i18n | LÖVE 级 2D 游戏 |
| `3d` | `2d` + 3D 模型、场景树、场景载入、GPGPU、风格化、昼夜、天气、体素、程序化生成、编辑器构件、DevTools | 3D 游戏 |
| `full` | 全部（<b>默认</b>） | 不确定时用这个 |
| `web` | 浏览器 WASM 可用的集合 | Emscripten 构建自动选中 |

档位只是起点，可以在其上继续微调（见下）。

## 单个模块开关

```sh
# 关掉不用的
cmake -B build -DEVENGINE_MODULE_DATABASE=OFF -DEVENGINE_MODULE_NETWORK=OFF

# 在某个档位基础上补一个
cmake -B build -DEVENGINE_PROFILE=2d -DEVENGINE_MODULE_DIALOGUE=ON
```

模块名就是 `src/modules/` 下的目录名，大小写都认：`-DEVENGINE_MODULE_map=OFF`
和 `-DEVENGINE_MODULE_MAP=OFF` 等价。

配置时会打印实际结果：

```
-- Module profile: 2d
-- Modules enabled: 32/56
-- Modules disabled: devtools;editor;plugins;database;rpg;...
-- Third-party groups: squirrel;sdl2;medialoader_image;...
```

## 依赖会自动处理

打开一个模块，它需要的模块会自动跟着打开 —— 开 `particles` 就会带上
`gpgpu`、`animation`、`ik`，不用自己列。

反过来，关掉别人还需要的模块是<b>配置期错误</b>，而不是等到链接期才冒出一堆
undefined reference：

```
CMake Error: Module 'devtools' needs 'procgen', which was disabled with
-DEVENGINE_MODULE_procgen=OFF. Disable 'devtools' too, or re-enable 'procgen'.
```

按提示二选一即可。

## 脚本侧不用改

启动脚本按构建产物绑定模块，被裁掉的模块不会出现在根表里。如果你的游戏脚本
要兼容多种档位，用 `has_module()` 判断：

```squirrel
if (has_module("particles")) {
    particles.update(dt);
}
```

`eve.moduleList` 是这次构建实际绑定的 `{ slot, cls }` 列表，可以直接遍历查看。

## 哪些模块不能裁

`window`、`graphics`、`event`、`filesystem`、`timer` 是引擎运行的前提，不可关闭。

另外两处目前限制了裁剪范围，配置时会点名提示：

- <b>DevTools</b> 直接引用了 `scene` / `physics` / `procgen` / `particles` / `audio` / `ui`，
  要裁这几个得先 `-DEVENGINE_MODULE_DEVTOOLS=OFF`。
- <b><code>ui</code></b> 被命令行的 `eve mcp` 子命令引用，目前不可裁，所以 `minimal` 档也带着 ImGui。

## 能省多少

linux-debug 实测：

| 档位 | 模块数 | 二进制 | 相比 `full` 少掉的第三方 |
|------|--------|--------|--------------------------|
| `full` | 56 | 233 MB | — |
| `3d` | 45 | 198 MB | PocoData、SQLite |
| `2d` | 32 | — | 上述 + DTL |
| `minimal` | 19 | — | 上述 + OpenAL、Box2D/box3d、音频编解码、assimp、medialoader model/sound |

Release 构建的绝对体积小得多，比例接近。

## 加新模块

在 [`cmake/module_manifest.cmake`](../../cmake/module_manifest.cmake) 里声明一次即可，
链接列表、第三方依赖、脚本绑定和依赖闭包都从那里派生：

```cmake
eve_declare_module(NAME mymodule LAYER 4
                   DEPS graphics data
                   THIRDPARTY poco
                   SCRIPT MyModule SLOT mymod
                   GROUP 3d)
```
