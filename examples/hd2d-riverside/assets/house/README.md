# Riverside cottage

Original low-poly cottage authored for this EVEngine scene. The actual model is
`cottage.obj`, with `cottage.mtl`; the four textured materials reference
`../terrain.png`. Preserve that directory relationship when importing elsewhere.

- Y up; front faces +Z; origin at ground center.
- 3350 triangles, 10 material groups; 32 world units per map tile.
- Structural footprint: X [-88,88], Z [-42,48]. Roof overhang: X [-100,100].
- Height: 154 units, including chimney cap.
- Windows and door are modeled exterior detail; there is no navigable interior.
- Regenerate with `python ../../build_house.py` from this directory, or run the
  script by its absolute path from any directory. Source geometry and palette
  live in that script. The runtime imports the OBJ through `model3d`.

The sibling `cottage-model.zip` packages the OBJ/MTL and referenced texture for
import into a DCC application. Mesh normals, per-face UVs and materials are
included; roof slates, chimney, timber beams, window frames and handle are mesh
geometry, not a flat house image.
