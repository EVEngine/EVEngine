# 山体平滑融合

四块独立 3D 地形：左侧普通 Raise，右侧 SmoothRaise；前排手工高度图山体，后排 CPU 岩石模型烘焙。
所有面板使用相同的程序噪声基础地形，橙色为普通 max，绿色为平滑融合。

运行：`make run/linux-debug GAME=examples/terrain-smooth-mountain`。
无外部模型资产；模型面板用现有 marching-cubes rock recipe 创建 MeshBuild。
启动完成打印 `SMOOTH_MOUNTAIN_READY`。烟测：
`MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh terrain-smooth-mountain`。

调整 `placePair` 内的 `smoothWidth`（高度融合宽度）、`edgeFade`（水平边缘淡出）、
`setCenter/setSize/setRotation`。高度图面板直接修改 authored；模型面板可将
`cpuMesh` 换成已有导入流程产生的 Y-up MeshBuild。顶面烘焙不保留模型 UV、材质、洞穴或悬挑。

烘焙一次后复用 stamp/coverage；coverage 传入 globalMask，localMask 使用 one。
网格使用融合后的高度构建，因此法线同步更新。本例没有物理世界；实际项目需在同次编辑发布后
重建对应碰撞。相邻地形瓦片共用世界坐标、印章参数和边缘采样规则。
