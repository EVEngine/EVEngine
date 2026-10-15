# Animal asset credits

- **Toon Cat FREE**, Omabuarts Studio — [original Sketchfab model](https://sketchfab.com/3d-models/toon-cat-free-b2bd1ee7858444bda366110a2d960386), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). `assets/cats/toon-cat.glb`, 2,636 triangles, embedded coat texture, `Scene` animation.
- **Tuxedo Cat Animated 2.0**, DreamNoms — [original Sketchfab model](https://sketchfab.com/3d-models/tuxedo-cat-animated-20-783fcb78b55b4394a212c2b6392e1113), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). `assets/cats/tuxedo.gltf` and `scene.bin`, 6,376 triangles; IdleNorm, WalkCycle, IdleTailSwoosh, SitDown, IdleSit, StandUp.

Downloaded from the [steam-frame-nix source mirror](https://github.com/lhns/steam-frame-nix/tree/6300093c3076816350ca27eec0e526f84a55f7b7/modules/pet/sources), with its source-specific notices preserved beside both models. Toon Cat and binary buffers are unmodified. In the Tuxedo glTF JSON, the second unused `Lamp` node was renamed to `Lamp_62` (unique node names are required by the engine skeleton importer); all indexed geometry, animation channels and binary data remain intact. This example also changes runtime transforms and material presentation. No endorsement by the artists is implied.

Architecture, generated plants and decorations are authored in this example. No RUN-only cat assets are included.

## Garden geometry

The three seeded oak/aspen variants were generated with [EZ-Tree by Daniel Greenheck](https://github.com/dgreenheck/ez-tree/tree/dcf309bd86bd521083d9c70f01f2de45fdc7c457), MIT. Source and license are retained under `garden/vendor/ez-tree`, along with presets and `garden/build-trees.mjs`. Imports were adjusted for native Node ESM; the preset-loader import is replaced because explicit preset files are supplied by the exporter. Tree geometry is rescaled to metres, Y-up. No upstream LFS texture assets are included. The leaf-spray texture is authored procedurally for this example; bark uses EVEngine’s built-in procedural recipe.
