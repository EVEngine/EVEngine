# Unity native Texture2D admission

The collection importer recognizes serialized Texture2D assets separately from Mesh assets. Embedded single-image 2D BC3/DXT5 textures are converted into image/3 sources with the same GUID-derived image identity used by material references. Material texture references accept both native serialized type 2 and imported type 3. Other native texture formats and streamed payloads remain explicitly unsupported.

The decoder validates dimensions, decoded allocation budget, exact mip-chain length, hexadecimal data, duplicate required fields, and mip count before publication. BC3 alpha uses both endpoint interpolation modes; color always uses four opaque RGB565 colors. Partial edge blocks are cropped. Unity bottom-left row order becomes canonical top-down row order. Serialized m_ColorSpace 1 selects sRGB, and 0 selects linear. No transfer operation is applied to alpha.

Every source mip is decoded independently, vertically flipped within its own dimensions, and
stored as a complete `rgba8-mips` chain. Cook accepts linear or sRGB packed chains and converts RGB
transfer per texel while preserving alpha. Material bindings translate the serialized wrap and
filter settings. The complete original asset remains under `sources/unity`; anisotropy is not part
of the current portable sampler contract.

The two supplied Pine textures provide real-source evidence. The 1024x1024 texture retains all 11
mips in 5592404 RGBA bytes, and the 2048x2048 texture retains all 12 mips in 22369620 bytes; both
match the exact sum of their level dimensions. `build/tve-native-textures-mips.eva` and its
four-chunk Windows Vulkan EVPACK pass full validation. Their SHA-256 values are
`eaa771be61389141e02ce308bb7d0779dafeaefd912d539247b3212fd44a6138` and
`b8f96d15120ca2dc303d3ac3f9e693bb16562cde92605ef1f7a647c1df443afe`.

Implementation references:
- [Microsoft BC3 layout](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression)
- [Unity raw mip ordering](https://docs.unity3d.com/kr/2021.3/ScriptReference/Texture2D.GetRawTextureData.html)
- [Unity pixel row ordering](https://docs.unity3d.com/ja/2023.2/ScriptReference/Texture2D.GetPixels.html)
- [AssetRipper texture importer metadata mapping](https://github.com/AssetRipper/AssetRipper/blob/master/Source/AssetRipper.Export.UnityProjects/Textures/ImporterFactory.cs)

Ownership remains local to the synchronous importer: borrowed input, owning unpublished result, no callbacks, GPU resources, external writes or retained pointers. The change uses the existing image/3 full-chain contract and introduces no new module boundary.
