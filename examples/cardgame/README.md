# Cardgame — 卡牌工具模块示例

演示 `eve.Card()`：扇形手牌布局（间距 / 弧高 / 悬浮放大 / 移动速度）、抽牌洗牌、
敌方手牌翻面偷看、拖拽出牌 / 弃牌、费用不足自动置灰，以及一个实时调节全部布局参数的配置面板。

示例使用 MrEliptik 的
[Stylized Playing Cards Pack](https://mreliptik.itch.io/playing-cards-packs-52-cards)
黑底套装显示完整 52 张牌。原始 655×930 PNG 被等比例缩小为 262×372，降低示例的磁盘与显存开销；
素材采用 CC0，许可文本保存在 `assets/playing-cards/LICENSE.txt`。

## 运行

```bash
make run/<platform>-debug GAME=examples/cardgame
```

## 操作

| 输入 | 作用 |
|---|---|
| 左键拖拽 | 拖到出牌区打出 / 弃牌区弃掉 / 手牌区松手归位 |
| 左键点按 | 查看手牌 |
| `1` | 抽一张牌 |
| `2` | 偷看敌方手牌 |
| `R` | 重置：洗牌并发牌 |

左侧面板可实时调节布局参数；卡牌定义以 JSON 注册（`registerCardsFromJson`）。
卡牌数据在 `data/cards.json`，改卡牌不用碰脚本，保存后热重载即生效。
