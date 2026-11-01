# Native house component library

`components.json` uses the existing HouseGen component-library format. Its model references are valid
Kenney Modular Buildings assets copied from the repository HouseGen fixture. Their CC0 license is
retained in `components/License.txt`.

The riverside presentation consumes HouseLayout component IDs, positions and rotations and builds
its meshes using native Procgen recipes. It does not load these GLB meshes for rendering; they keep
the shared component library usable by the standard HouseGen model backend as well.

Appearance and palette settings live in `../../house-native.nut` and `../../house-settings.nut`.
There is no offline Python or OBJ house generation step.
