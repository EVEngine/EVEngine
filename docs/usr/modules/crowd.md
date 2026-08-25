# 群体与 RTS 移动（Crowd）

**脚本入口：** `eve.Crowd()`

Crowd 是 UI/渲染无关的连续流场与群体模拟库。项目负责把地形、Tilemap 或建筑占用转换为
`setCellCost/setBlocked`，并把 `getAgentState` 映射到 ECS、Scene 或自定义渲染器。

## 稳定身份

紧凑 SOA 存储使用 swap-pop，普通 `addAgent` 返回的 slot 在删除其他单位后可能改变。
编辑器选择、存档、网络实体和 ECS 绑定应使用稳定逻辑 ID：

```squirrel
crowd.resizeField(64, 64, 1.0, 0.0, 0.0);
crowd.buildFlowField(32, 32);
crowd.addNamedAgent("unit.alpha", 2.0, 2.0, 0.0, 0.35);

local slot = crowd.getNamedAgentIndex("unit.alpha");
local state = crowd.getAgentState(slot);
```

- 稳定身份：`addNamedAgent/hasNamedAgent/getNamedAgentIndex/getAgentStableId/removeNamedAgent`。
- 匿名/批量模拟：`addAgent/removeAgent/clearAgents/getAgentCount`；适合粒子式、无需持久
  逻辑身份的群体。
- 地形流场：`resizeField/setBlocked/setCellCost/getCellCost`、
  `addFlowGoal/clearFlowGoals/build/buildFlowField`、`flowAtWorld/flowAtCell/costAtWorld`。
- 流场查询：`isFieldBuilt/getFieldWidth/getFieldHeight/getCellSize/getFieldOriginX`
  `/getFieldOriginY/isReachable`；编辑器可据此显示网格、目标可达性与地形覆盖范围。
- 单位控制：`setAgentAction/setAgentTarget/clearAgentTarget/setAgentSpeed/setAgentAccel`
  `/setAgentTurnRate/setAgentRadius/setAgentData/setAgentPosition/getAgentState`，以及
  `getAgentAction/getAgentData/getPositions/getHeadings`。
- `CrowdAgentState` 快照字段：`action`、`data`、`heading`、`speed`、`vx`、`vy`；
  快照仅用于把模拟结果同步到 ECS、动画和渲染状态。
- 容量与默认值：`setMaxAgents/getMaxAgents`、`setDefaultSpeed/setDefaultTurnRate`
  `/setDefaultRadius`、`setArriveRadius`。
- 邻域行为：`setSeparationRadius/setPerceptionRadius`、`setSeparationWeight`
  `/setAlignmentWeight/setCohesionWeight/setGoalWeight/setWanderWeight`。
- 解算约束：`setResolveOverlaps`、`setClampToField`；配置完成后调用 `step(dt)`。

[`examples/composable-editor/gameplay_components.nut`](../../../examples/composable-editor/gameplay_components.nut)
演示把正在编辑的 Heightmap 转换成移动代价、用稳定 ID 创建 RTS 单位，再逐帧同步回 ECS；
这套桥接属于项目代码，并非固定 RTS 编辑器。
