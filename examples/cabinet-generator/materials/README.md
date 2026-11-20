# 柜子 PBR 母材质

全部 PNG 为本示例的确定性程序化素材，256×256，可循环平铺，每块纹理覆盖一米。
wood：细木纹；lacquer：细腻漆面；brushed：拉丝金属。

每种包含七张贴图：

- albedo：基色（染色由各部位 tint 控制），alpha=255。
- normal：切线空间 XYZ 法线，线性数据。
- roughness：感知粗糙度，线性数据。
- metallic：木/漆为 0，金属为 1，线性数据。
- ao：微表面 AO，线性数据，不包含场景接触阴影。
- orm：R=AO/G=roughness/B=metallic，便于使用标准 PBR 管线。
- packed：RG=法线 XY/B=AO/A=1−roughness，供本示例的现有 packed-normal 路径使用。

示例实际加载 albedo+packed；metallic 按分组单独赋值，其余贴图供检查和其他材质管线使用。
将它们作为一套共同替换，避免法线、凹槽与粗糙度错位。packed 数据不得按 sRGB 解码。
当前不使用高度位移；起伏只用于计算细微法线。

重建：`python3 examples/cabinet-generator/generate_materials.py`（Pillow 依赖仅在重建时需要）。
