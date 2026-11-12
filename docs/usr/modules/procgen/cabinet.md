# 参数化柜子 mesh.cabinet

通过 `procgen.newParams()`、`procgen.buildMesh("mesh.cabinet", p)` 获取 Result，检查 `.ok` 后使用 `.value`。
CPU C++ 入口为 `generateCabinet(params)`，返回 `Result<MeshBuild>`；失败不修改调用方数据。
无需资源、随机数、ECS 或额外模块。相同输入生成相同顶点和分组，浮点计算不承诺跨编译器逐位一致。

| 参数 | 默认 | 含义 |
|---|---|---|
| cabinetWidth / cabinetHeight / depth | 1.8 / 2.0 / 0.55 | 米；cabinetHeight 含柜脚，装饰顶线保持此高度 |
| thickness | 0.025 | 板厚 |
| columns | 2 | 上部横向分柜，1–16 |
| levels | 3 | 上部储物层数，1–16；内部层板为 levels−1 |
| doors | 2 | 上部平分的门扇数，0–16；0 为开放柜 |
| drawers | 2 | 下部全宽抽屉，0–16；存在时共占内部高度 35% |
| legs / legHeight | true / 0.15 | 四脚开关及高度 |
| style | modern | european / chinese / minimal / modern |

欧式有框线、叠层顶线和收分圆脚；中式有门框、直棂、叠层顶线和方脚；
简约使用无外露把手平板；现代使用平板和条形拉手。示例按风格和部位配置 PBR 贴图与材质参数。
这四种是几何与材质预设，不包含雕花或历史家具复刻。

Y 朝上、XZ 居中、地面 Y=0、前面 +Z；尺寸是柜体尺寸，装饰顶线略超宽深，把手略向前突出。
分组：body、back、dividers、shelves、legs、cornice、door.N、drawer.N；不存在的部件无分组。
门板使用 door.N，抽屉面板使用 drawer.N；饰条、拉手分别为对应的 `.trim`、`.hardware` 子分组。
抽屉底板、侧板和背板归入 drawer.N.interior。可用 copyGroup 单独上传、赋材质；
若要开关门/抽屉，需对同一 door.N/drawer.N 前缀下的所有部件应用相同铰链/滑轨变换。
每个分组有 `getMeta("material." + groupName, "")` 材质角色：exterior/interior/trim/hardware/legs。
角色只描述部位，配方不加载纹理、不拥有 GPU 材质。UV 按米投影；门板竖纹，抽屉面板与顶线横纹。
门和抽屉默认关闭；生成器不维护铰链、动画、碰撞或可变运行状态。

浮点尺寸须有限、为正且不超过 100，板厚至少 0.002；负数、未知风格、越界计数和
不足以容纳板材/抽屉/层板的组合返回 InvalidArgument，不自动裁剪布局。
元数据由既有 RecipeDescriptor 提供，构建复杂度随部件数线性增长；配置变化时生成，逐帧复用网格。
此配方不增加模块依赖、持久格式、能力提供方或生命周期关系，沿用 procgen 的网格生成和上传边界。

宽高使用 cabinetWidth/cabinetHeight，避免与 Params.setSize 的网格维度域冲突。

```squirrel
local created = procgen.newParams();
if (!created.ok) throw created.status.summary;
local p = created.value;
p.setFloat("cabinetWidth", 2.4);
p.setFloat("cabinetHeight", 2.1);
p.setFloat("depth", 0.6);
p.setInt("doors", 4);
p.setInt("levels", 4);
p.setInt("drawers", 3);
p.setBool("legs", true);
p.setString("style", "chinese");
local generated = procgen.buildMesh("mesh.cabinet", p);
if (!generated.ok) throw generated.status.summary;
// generated.value 为 CPU 网格；通过 uploadMesh 上传，或用 copyGroup 拆分。
```


## 示例 PBR 材质

示例的 `cabinetStyleMaterials()` 明确配置 exterior/interior/trim/hardware/legs 五个角色，
可分别修改 tile、tint、metallic；不是把一个纯色材质套到整柜。

| 风格 | 柜体/面板 | 饰条 | 把手 | 内部 | 柜脚 |
|---|---|---|---|---|---|
| 欧式 | 暖橡木 | 黄铜 | 黄铜 | 浅木 | 橡木 |
| 中式 | 深色木 | 深色木 | 古铜色金属 | 浅木 | 深色木 |
| 简约 | 象牙白漆面 | 同面板（存在时） | 无外露把手 | 浅木 | 浅木 |
| 现代 | 蓝灰漆面 | 同面板（存在时） | 拉丝钢 | 浅木 | 拉丝钢 |

三个 256×256 可重复平铺母材质 wood/lacquer/brushed 均包含 albedo、normal、roughness、
metallic、ao、orm、packed 七张对齐 PNG。生成脚本和素材随示例分发，无需运行时生成或下载。
ORM = R:AO/G:roughness/B:metallic；当前示例复用引擎已有 packed-normal 渲染路径：
RG:normal XY、B:AO、A:smoothness（1−roughness），每个部位单独设置 metallic（木/漆=0、金属=1）。
贴图以循环采样加载；缓存三个母材质，切换风格/布局不会重复上传纹理。不要同时挂一个会覆盖
packed-normal 状态的 Material 到这些 Renderable3D 上。
法线由同一个微小高度场生成；木纹起伏小于 0.2 mm、漆面小于 0.01 mm，未开启几何位移。
AO 是纹理微凹槽遮蔽，不包含柜内接触遮蔽；示例提供低分辨率棚拍环境反射、带阴影主光、补光和地面；接触 AO 仍由渲染场景提供。

重建贴图：`python3 examples/cabinet-generator/generate_materials.py`（需要 Pillow）。
素材合同检查：`python3 -m unittest discover -s examples/cabinet-generator -p test_materials.py`。

欧式/中式的柜体顶板位于装饰顶线之下，避免共面闪烁；双开门把手成对靠近中缝。
