# EVEngine `.eva` 源资产包与 `.evpack` 运行时包规范

日期：2026-08-29
状态：v1 实现候选；格式冻结前仍须在受支持主机完成 Vulkan/WebGPU 实机认证
适用范围：编辑器资产导入、跨平台 Cook、运行时资源装载、热重载和第三方素材迁移

## 1. 目标

EVEngine 定义两种职责不同、不可互换的包：

- `.eva`（EVEngine Asset Archive）是可编辑、可迁移、跨工具和跨平台的源资产包；
- `.evpack`（EVEngine Runtime Pack）是由 `.eva` 和项目源资产确定性 Cook 得到的只读运行时包。

UE5、Unity、glTF、FBX、USD 和 DCC 文件都是导入源，不进入 EVEngine 的核心资产模型：

```text
UE5 / Unity / glTF / FBX / USD / images / audio
                        │
                  importer adapters
                        ▼
                  canonical .eva
             source semantics + provenance
                        │
             validate → resolve → cook
                        ▼
        .evpack[platform, backend, quality]
```

设计成功的判据不是“能解开一个 UE/Unity 文件”，而是：

1. 同一份 `.eva` 能为 Windows、Linux、macOS、Android、iOS 和 Web 生成目标包；
2. UE5 与 Unity 的等价素材进入同一 canonical schema，运行时不感知来源引擎；
3. 导入、迁移或 Cook 失败不会发布半完成资产；
4. 每个运行时依赖、平台差异、能力降级和许可来源均显式可诊断；
5. 不执行不可信包中的 Blueprint、Mono/.NET、native plugin 或任意脚本。

## 2. 非目标

- `.eva` 不是 `.uasset`、Unity serialized file 或项目目录的换后缀副本；
- 不承诺运行 Unreal Blueprint、Unity MonoBehaviour 或第三方原生插件；
- 不建立通用 `GameObjectAsset`、`ComponentBlob` 或无类型属性袋作为核心模型；
- `.evpack` 不作为可编辑源数据、长期归档或跨平台交换格式；
- 第一版不追求完整 JSON Schema、任意 Shader Graph 或任意供应商私有节点翻译；
- 包容器不承担许可证判定，只保存用户确认或 importer 采集的事实与策略标签。

## 3. 权威状态与身份

### 3.1 权威关系

- 项目导入后，`.eva` 中的 canonical asset definition 是源资产权威状态；
- UE/Unity 原文件是 provenance 指向的外部 source，不与 `.eva` 同时作为可变真源；
- `.evpack` 是可删除、可重建的派生物；不得反向编辑并覆盖 `.eva`；
- Editor AssetDatabase 是索引和状态投影，不复制 asset payload 成为第二真源；
- 运行时对象由各领域 registry/world 拥有，包只保存持久 `AssetRef`，不保存 runtime handle。

### 3.2 身份

每个资产使用现有 `eve::AssetRef`，canonical 文本为：

```text
asset://550e8400-e29b-41d4-a716-446655440000
```

路径只是 locator。重命名、换包或重新压缩不能改变资产 UUID。包本身使用独立的
`PersistentId`；人类可读名称使用 `LogicalId`，二者不能互换。

资产引用编码为强类型对象：

```json
{
  "kind": "asset",
  "asset": "asset://550e8400-e29b-41d4-a716-446655440000",
  "expectedType": "eve.texture/2"
}
```

`expectedType` 使用带版本的 canonical type（例如 `eve.image/2`），是 admission 时的类型约束，不是展示提示。解析到不同领域类型必须返回
`TypeMismatch`，不能返回空指针或替代资产后继续。

## 4. `.eva` 源资产包

### 4.1 物理容器

`.eva` v1 使用 ZIP64，必须满足以下 deterministic profile：

- entry 名为 NFC UTF-8、使用 `/`、不得以 `/` 开头；
- 拒绝 `..`、`.`、空路径段、反斜线、NUL、绝对路径和重复规范化名称；
- entry 按 UTF-8 字节序排列；
-时间戳、uid/gid、宿主权限和额外字段不进入 canonical hash；
- manifest 必须是根目录的 `manifest.json`，且未压缩大小受限；
- blob 可使用 Store 或 Deflate；算法不改变内容身份；
-不允许符号链接、硬链接、设备节点或加密 entry；
- ZIP comment 不承载语义，读取时忽略。

选择 ZIP64 是为了让源包容易检查、修复和被普通工具解开。ZIP 元数据不是格式契约；
canonical hash 由规范化 manifest 和 entry 内容计算。

实现使用固定版本 utf8proc 2.11.3（Unicode 17 数据）校验完整 NFC，并以
`STABLE|COMPOSE|CASEFOLD` 计算跨平台碰撞键；因此分解但 canonically equivalent 的名称、
以及 `Étage`/`étage` 这类非 ASCII 大小写碰撞都会在写入和读取 admission 阶段拒绝。
依赖发布包使用固定 SHA-256，不能回退为 ASCII-only 或依赖宿主 locale 的比较。
writer 对每个 entry 使用固定参数 raw Deflate，仅当结果严格小于 Store payload 时采用；
reader 同时接受方法 0/8，在分配前检查 ZIP64 decoded size 与总预算，要求 Deflate 消费完整
输入并精确产生声明长度，最后在解压后校验 CRC。压缩算法选择不进入内容身份。

### 4.2 逻辑布局

```text
manifest.json
schemas/                 optional, package-local extension schemas
assets/<uuid>/asset.json canonical domain definition
assets/<uuid>/source/    optional editable source payload
blobs/sha256/<digest>    immutable, content-addressed large payload
previews/                editor-only derived previews
reports/import.json      importer diagnostics and unsupported features
signatures/              optional detached signatures
```

核心资产定义不得依赖 ZIP entry 的偶然顺序。`source/`、`previews/` 和 `reports/`
不进入运行时 dependency closure，除非 manifest 明确把某个 entry 声明为 Cook 输入。

### 4.3 Manifest v1

```json
{
  "schema": "eve.asset-archive",
  "schemaVersion": 1,
  "packageId": "018f6f22-2490-7ad2-bf58-4f1dbca31040",
  "packageName": "vendor.magic-map",
  "packageVersion": "1.0.0",
  "unknownFields": "preserve",
  "assets": [
    {
      "asset": "asset://550e8400-e29b-41d4-a716-446655440000",
      "type": "eve.terrain-material",
      "schemaVersion": 1,
      "definition": "assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
      "contentHash": "sha256:...",
      "tags": ["terrain", "biome:temperate"]
    }
  ],
  "dependencies": [],
  "entrypoints": {
    "default": "asset://550e8400-e29b-41d4-a716-446655440000"
  },
  "provenance": {
    "path": "reports/import.json",
    "redistribution": "project-only"
  }
}
```

Manifest 的 `unknownFields` v1 固定为 `preserve`：支持的 reader 必须在编辑并重写源包时
保留未知字段，但未知字段不能改变已知字段的验证语义。未知 `schemaVersion` 必须拒绝，
不能用字段保留代替版本兼容。

### 4.4 通用资产类型

第一批 canonical 类型如下：

| Type ID | 权威内容 | 典型输入 |
| --- | --- | --- |
| `eve.image/2` | 未压缩/高质量源图和显式 transfer/primaries 色彩语义 | Texture2D、Unity Texture、PNG/EXR |
| `eve.texture` | 采样、色域、通道和 mip 策略 | UE/Unity texture import settings |
| `eve.mesh` | mesh primitives、LOD、skin、morph、bounds | StaticMesh、Mesh、glTF/FBX |
| `eve.skeleton` | 骨骼层级、bind pose、retarget metadata | SkeletalMesh、Avatar |
| `eve.animation-clip` | typed tracks、events、root motion | AnimSequence、AnimationClip |
| `eve.material` | EV shading model 和 typed parameters | UE Material Instance、Unity Material |
| `eve.scene-template` | typed node hierarchy 和领域 links | Blueprint/Prefab 的数据子集 |
| `eve.terrain` | heightfield、layer fields、tiles、尺度 | Landscape、Unity Terrain |
| `eve.terrain-material` | layer resolver 与地形着色参数 | M4、Terrain Layer |
| `eve.pcg-graph` | EV PointGraph/SpatialData 语义 | UE PCG、Unity splines/scatter tools |
| `eve.instance-set` | 分 Cell 的确定性实例结果 | ISM/HISM、Terrain trees/details |
| `eve.audio` | 源音频和循环/通道语义 | SoundWave、AudioClip |
| `eve.font` | 字体源与 glyph policy | Font assets |

每个领域类型拥有唯一 codec 和 migration chain。`eve::Value` 只用于 manifest、扩展
metadata 和 schema 驱动边界；mesh 顶点、像素和动画 key 等高频数据使用 typed binary blob。

### 4.5 坐标、单位和色彩 canonicalization

所有 importer 在发布 `.eva` 前转换到：

- 右手坐标系；`+X` 右、`+Y` 上、`-Z` 前；
- 长度单位为米，角度序列化为弧度；
- transform 使用 TRS，四元数顺序为 `[x,y,z,w]`；
- winding 为逆时针正面；切线基明确 handedness；
- UV 原点和纹理 row orientation 在 image/texture schema 中显式记录，Cook 后统一；
-颜色值为线性浮点，颜色纹理声明 `srgb`，数据纹理声明 `linear`；
- HDR 颜色空间声明 transfer/primaries；缺失时 importer 不得猜成平台默认；
- 法线贴图声明 `normalConvention: opengl|directx`，Cook 时按 backend 转换。

源引擎的原始 transform、unit scale 和转换矩阵保存在 provenance 中，只用于审计和重导入，
不参与运行时解释。

## 5. UE5 与 Unity 导入适配

### 5.1 Importer 契约

Importer 是可替换的工具侧 provider，输出 owning candidate，不直接修改 AssetDatabase：

```text
UntrustedSource
  → Parsed<SourceGraph, ImporterVersion>
  → Validated<CanonicalCandidates, EvaSchemaVersion>
  → Prepared<ArchivePublication>
  → Committed<ImportReceipt>
```

`prepare` 完成以下工作：解析、依赖闭包、坐标/色彩转换、schema 验证、哈希、许可来源记录、
临时包写入和完整重开验证。`commit` 才原子替换目标 `.eva` 并一次发布 AssetDatabase
记录。失败保留旧包和旧索引。

Importer ID、版本、源内容 hash、选项和工具版本共同构成 deterministic import key。
同一 import key 必须产生相同 canonical payload hash；预览缩略图和 wall clock 不参与。

编辑器侧由 `publishEvaAssetProjection` 将已经验证并发布的 manifest 投影为
`asset://<uuid>` 索引记录；`MemoryAssetDatabase::publishBatch` 先在 owning 副本上验证整个
包的 URI、GUID 与依赖，再以单一 generation 提交。批内任一冲突都不改变旧记录、依赖或
generation。AssetDatabase 只保存 definition 路径、hash 与检索元数据，payload 仍以 `.eva`
为唯一权威。

### 5.2 UE5 adapter

v1 优先支持 uncooked project content 和公开交换格式：

- Texture2D、StaticMesh、SkeletalMesh、AnimSequence；
- Material Instance 的 scalar/vector/texture/static-switch 参数；
- Landscape height/layer data、LandscapeGrassType；
- PCG Graph 的已声明节点子集、Spline/Volume/Point data；
- DataTable 的纯数据行；
- ISM/HISM/foliage 实例烘焙结果。

Material Graph、Blueprint、自定义 UObject、Niagara、RVT/VHFM、Nanite 私有 payload 和
第三方 C++ 节点不执行。adapter 必须把它们标为：

- `translated`：语义映射已通过 contract test；
- `baked`：转换为 image/mesh/instance-set 等静态结果；
- `preserved-source`：保留供未来 importer 使用，运行时不可用；
- `unsupported`：导入失败或由用户明确排除，不能静默忽略。

对于未版本化 property serialization 或需要 Blueprint VM 的 `.uasset`，v1 不伪装成通用
二进制反射器。无 UE 运行时路径使用版本化 `eve.unreal-landscape-import/1` adapter descriptor：
它直接引用下载内容中的 R16、纹理、weight 和静态实例源文件，并声明 M⁴ Material Instance、
LandscapeGrass 参数及必须报告的原生功能。descriptor 可以由 EVEngine/Fab 社区按产品版本提供，
不是要求最终用户启动 UE 后逐资产导出。若产品版本和 descriptor 不匹配，adapter 必须拒绝；
不能根据文件名猜测参数。未来直接 `.uasset` reader 必须携带匹配的 UE schema/mapping，并遵守
相同 canonical 输出和 diagnostics 契约。

### 5.3 Unity adapter

v1 支持：

- Texture、Mesh、Avatar/Skeleton、AnimationClip、AudioClip；
- Standard/URP/HDRP Lit 材质的已声明参数子集；
- Prefab 的 Transform 层级及已知 renderer/collider/light/audio components；
- TerrainData 的 heightmap、alphamap、TerrainLayer、tree/detail prototypes；
- ScriptableObject 中经 schema adapter 声明的纯数据；
- 场景中的静态实例烘焙结果。

MonoBehaviour、custom editor、C# assembly、Shader Graph 自定义节点和 Asset Store native
plugin 不执行。Prefab 只转换已知 typed components；未知 component 不能降级成可执行的
通用 property bag。

### 5.4 导入报告

每次导入必须产生结构化报告，至少包含：

- source engine/version、package/product identity；
- importer ID/version、options 和 import key；
- 每个 source object 到 `AssetRef` 的稳定映射；
- translated/baked/preserved/unsupported 数量和逐项 diagnostics；
- 丢失依赖、外部绝对路径、大小写冲突和平台限制；
- 坐标、单位、色彩和法线转换；
- 许可证来源、用户确认值和 redistribution policy；
- deterministic hash 与重导入差异摘要。

实现中的 `reports/import.json` 使用 `eve.asset-import-report/1`：manifest provenance 保存
其路径与 deterministic import key，admission 会重算 key、验证 source-object→AssetRef
映射只指向包内资产；迁移资产 schema 时同步刷新 canonical asset 版本、content hash 和
import key。报告被篡改、缺失或引用不存在资产时，包不能进入发布阶段。

## 6. 依赖模型

依赖边使用 stable `AssetRef`：

```json
{
  "from": "asset://...",
  "to": "asset://...",
  "kind": "runtime-required",
  "path": "material.baseColor"
}
```

v1 kind：

- `runtime-required`：Cook 和装载必须存在；
- `runtime-optional`：缺失行为由资产 schema 的显式 fallback policy 定义；
- `build`：只在 Cook 时需要；
- `editor`：只供编辑器使用；
- `source`：重导入来源，不进入输出；
- `platform`：仅特定 variant 需要，必须带 predicate。

依赖图必须无 `runtime-required` 构造环；允许的互相引用通过 stable link 延迟解析，不能靠
装载顺序和裸指针形成对象环。包卸载后，由 runtime asset registry 的 generation 检测 stale；
consumer 跨帧只保存 `AssetRef` 或领域 runtime handle。

`runtime-optional` 边必须带 `fallback` 对象，其 `behavior` 为 `omit-feature`、
`use-default` 或 `use-asset`，并必须提供稳定 `observableCode`；`use-asset` 还必须给出
canonical AssetRef。Cook 将这些策略写入资产 EVDEF 的 `dependencyFallbacks`，
因此即使可选目标未被打入包，runtime consumer 也能按稳定 code 降级和上报，
而不是由 Cooker 静默丢弃语义。

## 7. `.evpack` 运行时包

### 7.1 性质

`.evpack` 是单目标或显式多 variant 的不可编辑二进制容器，支持随机访问、mmap、范围读取、
内容校验和按 Cell/LOD 流式加载。它不使用 ZIP，也不要求运行时 JSON parser。

所有 v1 runtime definition 使用 `EVDEF\0\1` typed binary metadata，不携带 JSON 文本。
编码为 little-endian、object key 字节序 canonical、有限 double 和严格 UTF-8；decoder 在分配前
执行总字节、深度、Value 数量和单字符串预算，并拒绝未知 tag、重复/乱序 key、截断及尾随字节。
JSON 只存在于 `.eva` canonical definition 和工具侧 Cook 输入，运行时领域 loader 只消费 EVDEF
与各领域 typed bulk。

所有整数使用 little-endian；大端目标在 loader 边界转换。文件头 v1：

```text
magic[8]       = "EVPACK\0\1"
headerSize     u32
flags          u32
packageId      16 bytes
buildId        16 bytes
tocOffset      u64
tocSize        u64
manifestOffset u64
manifestSize   u64
signatureOff   u64
signatureSize  u64
headerHash     32 bytes SHA-256
```

v1 的可选签名块为固定 128 bytes：`EVSIG\0\1`、16-byte algorithm ID、
32-byte key ID、64-byte detached signature 和 8-byte zero padding。签名消息是已验证的
`headerHash`，因此间接覆盖 manifest、TOC、Chunk content hash 和依赖表。签名算法和
信任根由项目注入 `EvpackSigner`/`EvpackSignatureVerifier`；容器不内嵌购买凭证或
私钥。带签名但未配置 verifier 的包必须拒绝，不得降级为“未签名开发包”。

TOC entry 至少包含：asset UUID、type ID index、schema version、variant index、chunk kind、
offset、stored size、decoded size、alignment、codec、content hash 和依赖表 slice。
offset/size 的所有加法在验证后进行并检测溢出。

### 7.2 Chunk

建议 chunk kind：

- `definition`：领域强类型描述；
- `bulk`：vertex/index/pixel/audio/keyframe；
- `stream`：可独立请求的 mip/LOD/cell/page；
- `shader`：后端 shader 与反射布局；
- `dictionary`：可选压缩字典；
- `debug-name`：开发包可选，不参与功能。

Chunk 可独立压缩和校验。v1 codec 至少支持 `none`、`zstd`；纹理和音频使用其原生
block codec，不再外层重复压缩。加载器不得把 decoded size 由压缩流自行决定。

`eve.image/2` 的首个可执行 runtime slice 使用 `EVIMG\0\1` typed bulk：24-byte little-endian
header（magic、width、height、flags、reserved）后接紧密 RGBA8。PNG 在 importer prepare
阶段验证签名、chunk 边界/CRC、IHDR、IDAT zlib 流、filter 和解压预算；Cook 仅接受显式
`rgba8` capability，将 sRGB 源样本确定性转换为线性 RGBA8，并把 runtime definition 的
transfer 改为 `linear`、保留 `sourceTransfer`。JPEG 可作为源资产导入，但在登记安全且
确定性的 runtime decoder 前，RGBA8 Cook 明确返回 `Unsupported`，不能伪装成目标变体。

### 7.3 Variant key

variant 由能力而不是营销平台名决定：

```json
{
  "os": "windows|linux|macos|android|ios|web",
  "arch": "x86_64|arm64|wasm32",
  "graphics": "vulkan|webgpu",
  "textureFamilies": ["bc", "astc", "etc2", "rgba8"],
  "shaderFormat": "spirv-1.6|wgsl-1",
  "quality": "low|medium|high|cinematic",
  "features": ["mesh-shader", "sparse-texture"]
}
```

Cook 可以生成多个 `.evpack`，或在一个包内存放少量共享内容加多个 variant。运行时严格按
设备 capability 选择；不存在满足项时返回 `Unsupported`。允许 fallback 时必须在 Cook
manifest 中显式排序并在运行时 diagnostics/telemetry 中可观察。

## 8. Cook

Cook 输入为已验证 `.eva`、项目资产、目标 profile 和工具链锁定信息。阶段如下：

1. 解析并迁移到当前 schema；
2. 解析 root assets 和 dependency closure；
3. 检查许可证/发布策略，但不替用户作法律推断；
4. 按 target capability 选择/生成 mesh、texture、shader、audio variant；
5. 编译 PCG execution plan 和 scene template；
6. 分割 stream chunks，生成 TOC 和 dependency slices；
7. 重新打开临时包，完整验证 hash、引用、大小和 capability；
8. 原子发布，产生 `CookReceipt` 和 signed build manifest。

Cook key 包含：

- 所有输入资产 canonical content hash；
- schema/migration 版本；
- importer 结果版本；
- cooker、shader compiler、mesh/texture encoder 版本；
- target capability profile 和 quality；
-确定性选项。

不包含：wall clock、临时路径、线程数、主机名、ZIP entry 时间和 map 偶然顺序。

Cook 的确定性契约为：相同 Cook key 产生相同 decoded chunks、TOC 顺序和 build content
hash。容器签名和发行渠道 metadata 可以在内容 hash 外追加。

## 9. Schema 与迁移

- 包 envelope、每种 asset definition、typed binary blob 都有独立 schema ID/version；
- 当前发布版读取 `N` 和 `N-1`；更旧版本由离线 `eve asset migrate` 逐版本迁移；
- 未知新版本拒绝；不支持 downgrade；
- migration 消费旧 owning value/blob，生成新 candidate，不原地修改源包；
-未知字段策略由 schema 定义：核心结构通常 `reject`，扩展 metadata 通常 `preserve`；
- migration 后必须重新验证所有引用、hash、领域不变量和 dependency closure；
- 失败不覆盖输入或已发布输出。

包 schema 与资产 schema 不锁步。例如 `.eva` envelope v1 可以同时保存
`eve.material/2` 和 `eve.animation-clip/4`。

## 10. 来源、许可和供应链

`provenance` 至少记录：

```json
{
  "provider": "fab|unity-asset-store|local|other",
  "productId": "provider-specific-id",
  "productVersion": "source-version",
  "acquiredAt": "optional-wall-clock-metadata",
  "license": {
    "id": "user-confirmed-license-id",
    "ueOnly": false,
    "redistribution": "project-only|runtime-embedded|forbidden|unknown",
    "evidence": "project://licenses/receipt-or-note"
  },
  "sourceObjects": []
}
```

`acquiredAt` 不参与 deterministic hash。`unknown` 默认阻止公开发布 Cook，但允许本地检查；
用户可通过带审计记录的项目策略确认。Cook 只执行明确策略，不根据文件来源自动宣布合法。

`.evpack` 默认只保留最小许可标识和审计 hash，不包含购买凭证或用户隐私数据。

## 11. 安全加载边界

`.eva` 和 `.evpack` 均为不可信输入。v1 强制：

- 配置总文件大小、entry/chunk 数、单项 decoded size、总解压预算、递归深度和字符串长度；
- 在分配前验证 offset、size、count、stride、维度乘法和整数转换；
- canonical path 后再做目录归属检查，拒绝 path traversal 和大小写碰撞；
- 校验 manifest/header 后再信任 TOC，校验 chunk hash 后再交给领域 decoder；
- parser 不执行脚本、动态库、构造回调、网络请求或外部工具；
- importer 外部进程使用最小权限、资源限制和显式输入/输出目录；
- native code、C#/Blueprint 行为只能通过单独安装并授权的 EVEngine plugin 重写，不进数据包；
- 签名验证和内容完整性分离：hash 防损坏，受信签名证明发布者；未签名开发包可由项目策略允许；
- mount/registry publication 采用 prepare/commit，失败后旧 generation 和可观察状态不变。

## 12. 热重载与卸载

- `.eva` 文件变化触发重导入/重 Cook，不让运行时直接解释源包；
- 新 `.evpack` 完整 admission 后，以新 registry generation 原子替换；
-旧 runtime handle 解析为 `StaleHandle`，AssetRef 重新解析到新 generation；
- importer/cooker/loader 不持有跨任务临时裸指针；worker 返回 owning candidate；
- package A 先卸载时，A→B link 销毁；B 先卸载时，A 的解析返回 stale/missing；
- required dependency 缺失阻止替换；optional dependency 按 schema policy 显式降级；
- callback 在锁外、指定线程派发，并允许订阅者安全取消自身。

`EvpackRegistry` 的同步通知在执行 commit/unmount 的线程、registry 锁外派发。订阅回调返回
`Result<void>`；回调失败不回滚已经原子提交的 generation，而是进入 mount/unmount receipt
的 `callbackDiagnostics`，从而避免“状态已提交但操作返回失败”的歧义。

Registry 还是跨包 AssetRef 的唯一解析入口：`resolveAsset` 以
`AssetRef + expectedType + capabilities` 产生 generation-qualified `EvpackAssetHandle`，
`readAsset` 再返回 owning payload。同一 asset UUID 不允许被两个已 mount 包同时提供；
provider 替换后旧 asset handle 返回 `StaleHandle`，provider 卸载后返回 `NotFound`。

## 13. 旧 `.eva` 动画格式冲突

仓库当前已有以 `EVA 1` 开头的单动画文本格式，并将 `.eva` 作为 Mixamo 测试夹具后缀。
新 `.eva` 是通用 ZIP64 源资产包，两者不能长期共用扩展名。

迁移决策：

1. 新 `.eva` 的根 `manifest.json` schema 固定为 `eve.asset-archive`；
2. 旧动画文本夹具重命名为 `*.anim.txt`，例如 `Idle.anim.txt`；其内部 magic 在兼容窗口内
   保留 `EVA 1`，但它不再被视为正式资产格式；
3. `AnimImporter::importEva*` 迁移为明确的测试辅助入口 `importAnimationFixtureText*`，旧方法
   仅在一个发布窗口提供明确 deprecation；生产资产加载统一调用 canonical animation codec；
4. `eve asset migrate-animation Idle.anim.txt Idle.eva` 把旧文本包装成包含
   `eve.skeleton` 与 `eve.animation-clip` 的新源包；
5. 新 loader 先按容器 magic 判断，不允许仅凭扩展名把 ZIP bytes 送入旧文本 parser；
6. 格式冻结前完成仓库夹具、文档、热重载扩展名和转换工具迁移，不保留双真源。

## 14. 命令行与工具契约

建议的稳定用户入口：

```sh
eve asset import --from ue5 <source> --out <name>.eva
eve asset import --from unity <source> --out <name>.eva
eve asset inspect <name>.eva
eve asset validate <name>.eva
eve asset migrate <old.eva> --out <new.eva>
eve asset cook <name>.eva --target windows-x86_64-vulkan --out <name>.evpack
eve asset cook <name>.eva --target macos-arm64-vulkan --out <name>.evpack
eve asset cook <name>.eva --target ios-arm64-vulkan --out <name>.evpack
eve asset cook <name>.eva --target web-wasm32-webgpu --out <name>.evpack
eve asset diff <a.eva> <b.eva>
```

`migrate` 消费 owning archive，逐资产执行 N-1 migration，重算 definition hash、
重建并完整重开验证临时 `.eva`，最后才原子替换输出。当前已登记
`eve.image/1 → eve.image/2`；新版本和超出 N-1 窗口的旧版本均拒绝。

所有会失败或发布状态的 C++ API 返回 `[[nodiscard]] Result<T>`。建议的类型状态是：

```text
EvaSourceBytes → ParsedEva → ValidatedEva → PreparedEvaPublication → ImportReceipt
ValidatedEva + CookProfile → PreparedEvpack → VerifiedEvpack → CookReceipt
EvpackBytes → ParsedEvpack → ValidatedEvpack → PreparedMount → MountedPackHandle
```

析构只释放临时资源，不隐式 commit、save 或 mount。

## 15. MVP 与验收矩阵

格式只有在两个真实 importer 和两个真实 runtime target 使用后才能冻结。

### 15.1 UE5 journey

代表包：M4 或等价 terrain toolkit。验证：

- Texture/Material Instance/Landscape/Grass 配置进入 canonical terrain schemas；
- UE cm/左手系、DirectX normal 与材质通道转换正确；
- 不支持 Blueprint/RVT/VHFM 有逐项 diagnostics；
- 同一 `.eva` Cook 到 Windows Vulkan 与 Web WebGPU；
- 导入失败和 Cook failure injection 不覆盖旧产物。

### 15.2 Unity journey

代表包：Prefab + TerrainData + URP Lit materials。验证：

- GUID/fileID 引用稳定映射到 AssetRef；
-米制坐标、纹理色域、normal convention 和 prefab hierarchy 正确；
- Terrain alphamap/tree/detail 转 canonical terrain/PCG/instance schemas；
- MonoBehaviour 和 custom shader 明确报告；
- 同一 `.eva` Cook 到 Android Vulkan 与 Web WebGPU。

### 15.3 Contract tests

- `.eva` deterministic rebuild、解包/重包 canonical hash；
- `.evpack` deterministic TOC/chunks、随机访问和范围读取；
- AssetRef rename/move round-trip、类型不匹配和 stale generation；
- N-1 migration、未知新版本拒绝、未知字段 preserve/reject；
- required/optional/provider present/absent；
- path traversal、zip bomb、chunk bomb、重复路径、hash/signature 损坏；
- platform capability exact match、显式 fallback 和 no-match rejection；
- prepare/commit failure injection、旧包保持可用；
- Vulkan/WebGPU 对共享 backend-neutral 资源的可观察一致性。

## 16. 分阶段实施

1. **格式底座**：冻结 manifest、AssetRef、canonical hash、安全预算和旧 `.eva` 迁移计划；
2. **源包 vertical slice**：glTF/图片 → `.eva` → AssetDatabase，验证两个真实资产领域；
3. **运行时 vertical slice**：`.eva` → Vulkan/WebGPU `.evpack` → ResourceReader/Graphics；
4. **UE5 adapter**：M4 terrain slice，解析参数和源资源，不执行 Blueprint；
5. **Unity adapter**：TerrainData + Prefab + URP material slice；
6. **生产化**：增量 Cook、签名、远程 range streaming、补丁包和编辑器 UI。

每阶段必须接入真实 producer/consumer 并删除被替代路径；只有 schema、mock 或示例 JSON
不算完成。
资产容器的无图形裁剪边界由 `asset-core-only` profile 验证，只启用
common/data/asset 依赖闭包，不得因测试 `.eva/.evpack` 而隐式获得 window、graphics
或 Vulkan host。

当前实现的 Graphics consumer 是 `asset_graphics::EvpackGraphicsLoader`：它通过 capability
选择 variant，校验 `eve.mesh/1` 的 `EVMESH` typed blob、有限浮点、索引范围和 staging
预算，全部成功后才调用窄接口 `graphics::IMeshResourceFactory`。仓库内 Kenney GLB 夹具已
覆盖 `GLB → .eva → Cook → .evpack → ResourceReader → Graphics factory` 的真实纵向路径。
图片对应的 `EvpackImageLoader` 在任何 GPU 调用前验证 EVIMG magic、尺寸乘法、flags、
精确 byte size 和像素预算，再通过 `IImageResourceFactory` 上传；现有 Graphics 后端由
Result 化适配器接入，不能静默丢弃不支持的 sRGB 物理语义。

PCG runtime slice 不携带 UE/Unity 私有图对象。Cook 将
`eve.pcg-graph/1` 的 `terrain-layer-scatter-v1` 编译为版本化
`EVPCG_POINT_GRAPH 1` execution plan，并在 runtime definition 中记录唯一 output node 和
全部 `spatialSlots`。每条规则形成 `spatial.sample → spatial.project → filter.slope →
prototype/layer attributes` 分支，多个分支按源规则顺序确定性 merge；零密度规则显式
编译为 multiplier 为零的 density cull。`asset_procgen::EvpackPointGraphLoader` 先完成
capability/type/version/chunk/metadata/拓扑校验，再把调用方 terrain 复制绑定到全部声明槽，
最后才发布 owning `PointGraph`。UE M4 测试覆盖
`descriptor → .eva → Cook → .evpack → ResourceReader → PointGraph execute`，并验证原型、
图层属性与重复执行的确定性。

Unity Terrain/Prefab 的 runtime journey 也必须终止于领域对象，而不是“能从包中读回 JSON”。
`asset_procgen::EvpackTerrainLoader` 对 `EVTRN` magic、尺寸乘法、精确 byte size、有限高度、
单位/坐标系和 definition↔bulk 一致性完成 admission 后，构造 owning `Heightmap` 与
`SpatialData`；当前 SpatialData 只能表达等距 cell，非等距 X/Z spacing 显式返回
`Unsupported`。`asset_scene::EvpackSceneTemplateLoader` 校验 UUID、唯一 fileID、父引用、
循环、深度预算、有限 TRS 与单位四元数，再构造 owning `Scene::NodeDesc` candidate；挂载
仍由调用方通过 Result 化 Scene API 显式执行。Unity Android Vulkan/Web WebGPU 纵向测试
已覆盖 TerrainData/Prefab 导入、双平台 Cook、地形采样、坐标变换后的场景树，以及
definition/bulk 不一致和循环 hierarchy 的拒绝。

`asset_procgen::EvpackInstanceSetLoader` 是 `eve.instance-set/1` 的 typed runtime consumer：
它在分配前校验 EVINST header、记录数量与最小字节下界，并对每条记录验证
UTF-8 prototype、有限 TRS、单位四元数、非零缩放与精确尾部。UE M4 和 Unity
Terrain 纵向测试都从各自源坐标系导入，经 `.eva`/Cook/`.evpack` 后读回并校验
米制右手坐标和轴重排后的实例。

`asset_procgen::EvpackTerrainMaterialLoader` 将 `eve.terrain-material/1` EVDEF 终止于
owning `RuntimeTerrainLayer`，严格验证层数、UTF-8 source locator、有限正 tiling
及 `opengl|directx` normal convention。UE M4 和 Unity journey 分别验证 DirectX/
OpenGL 法线语义和米制 tiling，不再以“能读回 metadata”作为材质验收。

## 17. 架构规则适用说明

- **持久格式**：包和每种资产均有 schema/version/migration/unknown-field policy；
- **Result**：import/cook/migrate/mount/replace 均是不可丢弃的结构化 Result；
- **唯一权威**：`.eva` 是 canonical source，`.evpack` 与 AssetDatabase 是派生物；
- **短根与组合**：资产类型领域化，不创建通用 GameObject 根；跨资产使用 AssetRef；
- **生命周期**：registry generation 检测 stale，prepare/commit 原子发布；
- **线程与回调**：worker 只返回 owning candidate，锁外派发通知；
- **确定性**：import/cook key 排除 wall clock、路径和调度偶然性；
- **可选依赖**：present/absent 均测试，fallback 显式且可观察；
- **安全**：不可信字节经过 parsed/validated/prepared/committed admission；
- **跨后端**：Vulkan/WebGPU 共享领域语义，通过 capability variant 选择物理编码。
