# Village part models

Original EVEngine example fixtures, dedicated to the public domain under CC0-1.0.
No Easy Grid Builder source or assets are copied. Regenerate with
`python3 examples/building_village/assets/generate_parts.py`.

All models have one mesh, flat normals, Y-up and normalized bounds [-0.5,0.5].
`wall-window.obj` runs along X, `chair.obj` faces +Z, `roof-gable.obj` has its ridge
along Z. Models are loaded once by `villageLoadModels()` in `view3d.nut`; replace
its asset paths with models following the same pivot, dimensions and one-mesh contract.
Missing/invalid assets fail startup explicitly. Presentation tint is applied per instance.

The window aperture is visual; the logical wall remains a solid placement boundary.
The roof is a separate 4x3 building part at level 1, with a 0.08-cell visual overhang.
