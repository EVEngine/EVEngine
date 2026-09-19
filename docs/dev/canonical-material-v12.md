# Canonical material schema v12

Schema v12 extends the optional `vegetationAlpha` object from three to six
members. The exact members are `global`, `variation`, `detailFade`, `glancing`,
`camera`, and `constant`. The three new numeric values map directly to TVE 12.6
`_FadeGlancingValue`, `_FadeCameraValue`, and `_FadeConstantValue`; all five
numeric members are finite values in `[0,1]`.

The material stores only local authoring controls. The repeating 3D fade-noise
texture, its world-space tiling, and camera fade minimum and maximum remain
runtime state. They are attached with the coherent vegetation GPU field set, so
loading a material cannot retain a temporary resource pointer or publish a
partially configured fade source.

`eve.material/11` is the sole migration input. When `vegetationAlpha` is absent,
migration changes only `schemaVersion`. When it is present, migration requires
the exact v11 three-member object and adds `glancing=0`, `camera=1`, and
`constant=0`. Extra or malformed members are rejected before publication. The
migration preserves dependencies and the source archive.
