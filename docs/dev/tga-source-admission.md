# TGA source image admission

The TVE 12.6.0 package contains nine TGA textures. They use 24-bit RGB or 32-bit
RGBA, either raw true-color (type 2) or RLE true-color (type 10), with the version-2
footer and no extension/developer payload. All nine original files have passed
native import and cooking. Their cooked RGBA bytes, using explicit linear source
transfer for the pixel-preservation check, exactly match Pillow's decoded RGBA.
Local evidence is `build/tve-tga-check/pixel-comparison.json`; source art is not
redistributed.

The decoder follows the [Truevision TGA specification](https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html).
It admits non-paletted 24/32-bit raw/RLE data, both vertical and horizontal origin
directions, and bounded image IDs. RLE packet and output lengths are checked
before reading pixels. Legacy files without footers are accepted. When present,
extension/developer offsets bound the pixel stream. Premultiplied or unknown
extension alpha types are rejected; metadata color correction and gamma are not
applied. The canonical image definition remains authoritative for transfer.

The output owns top-down RGBA8 pixels. BGR(A) order is converted to RGB(A),
three-channel sources receive opaque alpha, and four-channel source bytes are
preserved. Decode is reentrant and retains no source pointers. Invalid packets,
truncated pixels or excessive output size return checked diagnostics; no partial
candidate is published. Palette, grayscale, interleaved and other pixel depths
are outside this admitted subset.

`eve.image/2` uses `encoding="tga"` for the source; runtime EVIMG remains RGBA8.
Unity collection import recognizes `.tga` and uses existing texture metadata to
select linear/sRGB semantics. Unity-specific normal processing remains unsupported.
Tests cover raw/RLE, four origin directions, RGB/RGBA, truncated streams, packet
overflow, invalid footer offsets, budgets and import/cook. They run in the
renderer-free asset-core profile as well as the full test binary.
