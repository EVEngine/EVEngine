# 贴花（Decal）

**脚本入口：** `eve.Decal()`

Decal 模块把血迹、污垢、破损等材质投射到世界几何体。运行时支持投影盒、
planar/triplanar 映射、纹理 atlas、生命周期淡入淡出、种类配额及材质通道强度。
Procedural Decal 可用确定性 seed 在加载或编辑阶段生成配套的 albedo、normal、params
纹理，不依赖外部商业纹理。

## Procedural 示例

```squirrel
persist decal = eve.Decal()
persist bloodAlbedo = null
persist bloodNormal = null
persist bloodParams = null

eve_init <- function() {
    bloodAlbedo = decal.bakePresetTexture(gfx, "blood-wet", 7, 256, "albedo")
    bloodNormal = decal.bakePresetTexture(gfx, "blood-wet", 7, 256, "normal")
    bloodParams = decal.bakePresetTexture(gfx, "blood-wet", 7, 256, "params")

    local id = decal.project(0.0, 0.01, 0.0, 0.0, 1.0, 0.0,
                             bloodAlbedo, "blood", 1.0, 0.12,
                             true, 7, 0.05, 8.0, 1.0)
    decal.setTextures(id, bloodNormal, bloodParams)
    decal.setStrength(id, 1.0, 1.0, 1.0, 0.0)
    decal.setProjection(id, "spherical", 4.0) // 也可用 "world" 世界对齐三平面 UV
    decal.setParallax(id, 0.05, 8.0, 24.0)
}

eve_update <- function(dt) {
    decal.update(dt)
}
```

纹理引用应保存在 `persist` 状态中，并至少比使用它们的贴花存活更久。三次调用必须
使用相同的 preset、seed 和 resolution。烘焙会分配纹理，不能放在 `eve_render` 或
每帧更新中。

## API 快查

| API | 说明 |
|---|---|
| `getName()` | 返回模块名 |
| `proceduralPresets()` | 返回逗号分隔的稳定预设目录 |
| `bakePresetTexture(gfx, preset, seed, resolution, channel)` | 生成 `albedo`、`normal` 或 `params` GPU 纹理；尺寸为 1..4096 |
| `bakeSbsprsTexture(gfx, xml, resolution, channel)` | 导入 Substance `.sbsprs` 文本并生成指定 GPU 纹理通道 |
| `project(x, y, z, nx, ny, nz, albedo, kind, size, depth, randomYaw, seed, fadeIn, lifetime, fadeOut)` | 投射贴花并返回运行时 id |
| `setTextures(id, normal, params)` | 安装可选法线和参数纹理 |
| `setStrength(id, normal, roughness, metallic, emissive)` | 设置各材质通道强度 |
| `setUvRect(id, x, y, w, h)` | 设置规范化 atlas 区域 |
| `setBlend(id, mode)` | 设置 `over` 或 `add` 混合 |
| `setProjection(id, mode, blendSharpness)` | 设置 `planar`、`triplanar`、`spherical` 或 `world` 投影 |
| `setParallax(id, scale, minLayers, maxLayers)` | 使用 params alpha 高度执行 POM；scale 为 0 时关闭 |
| `setEdgeFade(id, width)` | 设置投影体边缘遮罩羽化宽度，范围 0..0.49 |
| `remove(id)` / `clearAll()` / `count()` | 管理运行时实例 |
| `setLimit(kind, limit)` | 设置种类配额；超额淘汰最旧实例 |
| `update(dt)` | 推进淡入、生命周期和淡出 |
| `setEnabled(gfx, enabled)` | 开关图形后端的 decal pass |

params 通道布局为 roughness、metallic、emissive、height；height 同时用于 procedural decal
法线生成和 POM ray marching。建议从 `scale=0.02..0.08` 开始调节；层数范围必须满足
`1 <= minLayers <= maxLayers <= 64`。
