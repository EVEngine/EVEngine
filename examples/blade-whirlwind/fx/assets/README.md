# Replaceable masks

`wind-blade.tga` and `ink-streak.tga` are 512×128 RGBA masks generated for this example.
They contain no third-party artwork. Only alpha is sampled; the definition supplies the palette.
Replace the paths in `../styles.nut` with any engine-supported RGBA image.
Keep U tileable or fade its ends to zero for separated streaks; V spans the ribbon width.
Both masks are ordinary assets, with no engine rebuild or atlas regeneration required.
