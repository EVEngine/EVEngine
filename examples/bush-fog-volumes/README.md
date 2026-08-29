# Bush Fog Volumes

A focused 3D rendering check for improved procedural `mesh.bush` foliage in a
global atmospheric layer and two camera-correct analytic local fog volumes.
The six bushes use depth-writing masked/coverage foliage, overlap in depth, and
cross warm spherical and cool cylindrical fog. Atmospheric transparency remains
in the fog pass instead of being multiplied through every overlapping leaf shell.

```powershell
make run/win32-debug GAME=examples/bush-fog-volumes
```

Controls:

- `1`–`4`: fixed front, reverse, side, and high-angle depth/transparency checks
- `V`: cycle the four camera checks
- `F`: toggle fog for an immediate silhouette and blend comparison
- `P`: capture the current fixed view as `bush-fog-view-N.png`

The example rebuilds local froxels whenever the camera changes. Local volumes are
voxelized through the camera inverse view-projection, keeping their world-space
positions stable from every check angle. Foliage stays on the stable
masked/coverage path so internal leaf overlap cannot create translucent ghosts.
