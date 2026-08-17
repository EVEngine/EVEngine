# i18n 示例

多语言本地化最小演示：`eve.I18n` 载入 JSON 翻译表，点号键取值、`{name}` 占位符、
复数规则、默认语言回退与文件热重载。

## 运行

```sh
make run/macosx-debug GAME=examples/i18n
# 或 linux / win32 对应的 run/<platform>-debug
```

## 操作

- **1 / 2 / 3**：切换 英文 / 中文 / 法语
- **空格**：开关自动热重载（运行时修改 `locales/zh.json` 可即时看到新文案）

## 要点

- `loadFromFile` 从 VFS 读 JSON；`setDefaultLanguage` 提供缺失键回退。
- `getWithParams("greeting", {name = ...})` 做占位符替换。
- `getPlural("items", n)` 按当前语言的复数规则选 `one/other`（法语、俄语等有更多形式）。
- 每帧调用 `i18n.update(dt)` 检测文件修改并重载。