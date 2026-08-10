# Environment cubemap fixtures (test/Textures/env)

Derived 128×128 LDR PNG faces for IBL / lighting tests. Face order matches
`Graphics::newCubemap`: +X,-X,+Y,-Y,+Z,-Z (`px,nx,py,ny,pz,nz.png`).

## Sources (CC0 — Poly Haven)

| Fixture | Source asset | Notes |
|---------|--------------|-------|
| `studio_small_09/` | [studio_small_09](https://polyhaven.com/a/studio_small_09) | Soft indoor studio lighting |
| `kloppenheim_06_puresky/` | [kloppenheim_06_puresky](https://polyhaven.com/a/kloppenheim_06_puresky) | Outdoor clear sky / sun |

Authors: Poly Haven contributors (see each asset page). License: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).

## Regeneration

1k Radiance HDR downloads are not committed. To rebuild faces:

```bash
mkdir -p test/Textures/env/_src
curl -L -o test/Textures/env/_src/studio_small_09_1k.hdr \
  https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr
curl -L -o test/Textures/env/_src/kloppenheim_06_puresky_1k.hdr \
  https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloppenheim_06_puresky_1k.hdr
python test/Textures/env/_convert_hdr_to_cubemap.py
```

Requires: `opencv-python-headless`, `pillow`.
