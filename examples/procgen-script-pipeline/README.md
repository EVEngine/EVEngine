# Procgen Script Pipeline

纯 Squirrel 程序化生成示例，不使用节点图：网格采样、排除区和自裁剪通过普通函数组合，
最终 `PointSet` 在一个命名事务中原子提交。

```powershell
make run/win32-debug GAME=examples/procgen-script-pipeline
```

- 修改并保存 `main.nut`：`eve_reload` 会重建 `forest`；生成失败时保留上次结果。
- 按 `R`：更换根 seed 并重新生成。
- 控制台会输出每个命名阶段的输入/输出点数、系统 revision 和 seed。

示例目前用中央圆形排除区模拟道路或广场。未来的 Surface、Spline 和 Volume 输入可继续
返回 `PointSet`，不需要改变脚本编排形式。
