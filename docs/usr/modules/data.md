# 数据模块

**脚本入口：** `eve.DataModule()`

管理 ByteData/DataView、压缩、哈希以及 JSON/XML 文档。

## 基本用法

```squirrel
local data = eve.DataModule();
local bytes = data.newByteData(256);
local json = data.newJsonDocument();
```

## 对象关系与调用时机

`ByteData` 拥有字节；`DataView` 引用其中一段；`JsonDocument` / `XmlDocument` 表示解析树。DataModule 负责创建和编解码，文件读写仍由 Filesystem 完成。

## 目标导向指南

### 解析和保存 JSON

用 `decodeJson(text)` 得到文档，修改后调用 `encodeJson(doc, true)` 输出易读文本；网络传输可用非 pretty 格式减小体积。解析外部数据时必须处理异常或错误返回。

### 管理二进制缓冲区

`newByteData(size)` 创建拥有内存的缓冲区；`newDataView(data, offset, size)` 只引用一段范围。View 的生命周期不能超过底层 Data，跨线程前应明确所有权。

## 常见问题

- DataView 活得比底层 ByteData 久：会产生悬空引用。
- 信任网络 JSON 字段：解析成功后仍需做 schema/范围校验。
- pretty JSON 用于网络包：会无谓增加体积。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `decodeJson()`、`decodeXml()`、`empty()`、`encodeJson()`、`encodeXml()`、`getName()`、`isArray()`、`isObject()`
- `newByteData()`、`newDataView()`、`newJsonDocument()`、`newXmlDocument()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/data/`](../../../src/modules/data/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `data`。
