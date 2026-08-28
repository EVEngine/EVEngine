# KayKit 战棋示例

这是一个固定镜头的交互式 3D 战棋：4 名冒险者在 12×5 方格地图上迎战 6 种骷髅敌人。
`tactics` 模块负责棋盘占位、行动顺序、移动合法性和胜负目标；示例脚本负责 HP、技能、
敌方 AI 及表现层。

```sh
make run/linux-debug GAME=examples/tactics
```

## 操作

- 英雄回合会停下来等待玩家；当前英雄由顶部提示显示。
- 绿色格表示可移动位置，红色格表示当前技能可攻击的敌人。
- 鼠标左键点击绿色格移动，点击红色敌人攻击。
- 数字 `1`、`2`、`3` 切换当前英雄的三种技能。
- `Space` 暂停或继续，战斗结束后按 `R` 重新开始。

## 阵容与技能

- Alden（Knight）：Shield Bash、Cross Slash、Lion's Charge
- Lyra（Mage）：Arc Bolt、Rune Nova、Frost Lance
- Nyx（Rogue）：Quick Stab、Twin Fang、Smoke Knife
- Brom（Barbarian）：Axe Chop、Whirlwind、Earthbreaker
- 敌人：Bone Grunt、Crypt Guard、Grave Archer、Hex Adept、Bone Cutthroat、Ossuary Captain

每个角色都切换待机、行走、攻击和受击骨骼动画。攻击命中时还会播放 2D 精灵序列特效，
并显示向上飘动的伤害数字。免费 Skeletons 包只有 4 个不同角色模型，因此 6 个敌人是
6 种战斗定位，其中 Archer/Cutthroat 与 Guard/Captain 分别复用模型。

## 素材

- [KayKit Skeletons 1.1 FREE](https://kaylousberg.itch.io/kaykit-skeletons)，CC0 1.0
- [KayKit Adventurers](https://kaylousberg.itch.io/kaykit-adventurers)，本例文件取自作者的
  [Adventures 1.0 官方仓库](https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0)，CC0 1.0
- `assets/vfx/attack-vfx.png` 是为本例生成的 4×4 攻击特效图集
- `assets/fonts/DejaVuSans-Bold.ttf` 来自 DejaVu Fonts；许可证说明见同目录 `NOTICE.txt`

原始素材许可证随文件保存在 `assets/kaykit/*/LICENSE.txt`。本例仅收录运行时使用的免费
GLB 文件，没有加入付费扩展包内容。

## 架构边界

生命、伤害和技能不是 tactics 的权威状态。示例结算 HP 后，仅在单位阵亡时调用
`defeatUnit(subject)`，让 tactics 释放占位并评估 objective。正式游戏可以把这部分替换成
RPG/技能模块，而不改变战棋模块的棋盘与回合职责。
