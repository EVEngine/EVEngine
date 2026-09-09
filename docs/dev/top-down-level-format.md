# Tilemap 编辑文档与格式读写

`eve::level_editing::LevelDocument` 是关卡编辑的权威文档，保存有序 tile 层和对象层。
`TileBuffer` 保存每格完整的 32 位 GID；`Brush` 和 `EditorHistory` 修改同一份 buffer。
坐标、尺寸使用世界像素，`0` 表示空格。支持 orthogonal、isometric、staggered、hexagonal
方向参数；方向参数能保存不等于已验证每种方向的编辑器视图。

## 已实现的格式边界

| 格式 | 当前编辑支持 |
| --- | --- |
| `eve.level` v1，`.level.json` / `.evelevel` | 有限、稠密的 tile 层与对象层；保存和恢复 |
| `tiled.json`，`.tmj` / `.json` | 有限地图、数组 GID tile 层、对象层；导入、编辑、导出 |
| Tiled group、image、infinite/chunks、base64/压缩层 | 编辑器明确返回 Unsupported；不生成空白或被截断的成功文档 |
| RPG Maker、双网格逻辑/显示关联 | 运行时已有相应能力；本格式修复尚未将它们接成完整编辑文档 |

运行时 `map::loadMapFile` 的输入覆盖比编辑文档更广，不能用运行时导入成功来证明可编辑和可往返保存。
Tiled 的外部 TSJ/TSX 引用、内嵌 tileset、动画、Wang 元数据会原样保留，**不会在这里加载图片或解析规则**。

## 原生格式与兼容读取

```json
{
  "format": "eve.level",
  "version": 1,
  "orientation": "orthogonal",
  "width": 2,
  "height": 2,
  "tilewidth": 32,
  "tileheight": 32,
  "properties": { "music": "forest" },
  "layers": [{
    "id": "layer-1",
    "name": "Ground",
    "type": "tilelayer",
    "visible": true,
    "locked": false,
    "opacity": 1,
    "offsetx": 0,
    "offsety": 0,
    "properties": {},
    "width": 2,
    "height": 2,
    "data": [0, 0, 2147483649, 1]
  }]
}
```

- `format=eve.level`、`version=1` 标识原生格式。兼容历史省略 version 的 v1 文档，拒绝未知新版本。
- GID 输出为 `0..4294967295`，翻转位不丢失。C++ 的 `int` 接口按 32 位位模式传输；负值不是擦除命令。
- 旧原生 v1 可能保存负的有符号 GID，读取时迁移成同一位模式，下次保存输出无符号数。
  Tiled 输入不接受负 GID。
- 输入必须恰有 `width * height` 个整数 GID；非法数据类型、越界值、截断/多余格子均拒绝。
  单层最多 16M 格，地图尺寸和 tile 尺寸必须有效。
- 原生对象字符串 ID、层锁定状态保留。加载后新建对象/层会避开已存在的生成式 ID。
  Tiled 的已有数字 ID 保留，新建的字符串 ID 在导出时分配未占用数字 ID。

## 未建模字段的所有权

文档、层、对象各自拥有没有映射到强类型字段的 `Value` 数据，例如 tileset、投影参数、
对象 polygon、来源工具扩展字段。已映射字段不保留另一份可变副本，输出由文档当前值生成。

可直接编辑的字符串属性继续保存在既有 `properties` map 中；其他 Tiled 类型属性保留完整
属性记录。经原生文件中转时使用属性数组保留类型。显式将某个同名属性改为字符串时，
新字符串替换该类型属性，不输出重名属性。未知字段保留不表示编辑器理解或执行其语义。

JSON 输出使用 common 的 canonical Value serializer，键和字符串属性顺序确定，拒绝非有限数。
导入先构造独立候选文档；错误返回 `Result`，不向调用方发布半成品。
所有操作在文档 owner 线程同步执行，不保存调用方借用、不调用回调。

## 保存与路径

保存先完整编码，再在目标目录创建唯一临时目录，关闭临时文件后替换目标文件。
编码、写入或替换失败不会先截断旧文件；临时内容自动清理。这不承诺断电时的 fsync 级持久性。

相对资源路径原样保留。Save As 到其他目录时，宿主需同步搬运/重定位资源；当前 codec 不会自动重写资源路径。

扩展格式继承 `LevelFormat`，通过 `registerFormat(std::unique_ptr<LevelFormat>)` 注册；
`read/write/load/save` 返回的 `Result` 必须检查。新适配器需定义自己的未知字段策略和失败边界。

## 验证

```sh
make check/tilemap-editor JOBS=4
```

该入口编译真实的 CPU 编辑/地图代码，逐 case 运行格式往返、翻转恢复、混合地形绘制、
笔刷/撤销/重做/保存组合路径和保存失败测试，不启动 Vulkan 或伪造渲染后端。
需要 GCC C++20、已初始化的 zeroerr/ECS 子模块和 GLM。
通过 `TILEMAP_CMAKE_ARGS` 可传 `EVE_ZEROERR_SOURCE`、`EVE_ECS_SOURCE`、`EVE_GLM_INCLUDE`。
`-DEVE_TILEMAP_RUNTIME_TESTS=OFF` 验证不含 map/ECS/GLM 的纯文档读写构建。
主测试工程也注册这些 case；该快速入口不能代替完整引擎构建或编辑器画面验证。
