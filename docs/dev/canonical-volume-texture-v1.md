# Canonical volume texture schema v1

`eve.volume-texture/1` represents one immutable three-dimensional scalar source.
Its exact JSON members are `schema`, `schemaVersion`, `width`, `height`, `depth`,
`encoding`, `usage`, and `blob`. Version 1 admits positive 32-bit extents,
`encoding="r8"`, `usage="noise"`, and an asset-local `source.r8` blob. Voxels
are stored with X changing fastest, then Y, then Z.

Cook validates the exact source byte count and decoded-byte budget before
expanding every scalar to `(r,r,r,255)`. Runtime bulk uses the 24-byte `EVVOL`
v1 header: eight magic/version bytes, little-endian width, height and depth, and
a zero reserved field, followed by tightly packed RGBA8 voxels. The runtime
definition changes only `encoding` to `rgba8` and `blob` to `chunk:1`.

The graphics loader cross-checks the definition and bulk header before calling
the backend. A successful texture remains owned by its graphics factory until
released through the same factory. Loading and release are graphics-thread,
synchronous, and non-reentrant operations. Unsupported backends return a
structured `Unsupported` result without publishing a texture.

The Unity converter currently admits embedded, linear, one-mip text Texture3D
assets with serialized `m_Format=5` whose byte count proves one scalar per
voxel. Unity 6000.0.79f1 independently loads TVE's source asset as
`TextureFormat.R8` / `GraphicsFormat.R8_UNorm`, with 4096 raw bytes, bilinear
filtering, and Repeat on U, V and W. Streamed data, additional mips, color
transfer, and other source formats are rejected explicitly.

The TVE 12.6 source `Internal NoiseTex3D.asset` imports as exactly one canonical
16x16x16 volume. Its 4096 canonical bytes match Unity `_typelessdata` exactly
(SHA-256 `6ffaea889aaef0de8d416be8edd2e0b0ba75ca7809ccd1ab7b98d4c3074563ad`).
The Windows Vulkan EVPACK contains one definition and one bulk chunk for that
asset; the 16,384 expanded RGBA bytes have SHA-256
`9fe5d0ffb140ff669f694e3ee78fcb419eaca6032c521434e2de2baf466cb559`.
