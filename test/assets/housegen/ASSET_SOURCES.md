# House generator visual asset sources

Visual demos must use only the selected converted subset of these CC0 packages. Unit tests use
the manifest fixture in `test/housegen.cpp` and do not load external binary assets.

| Package | Source | License | Intended use |
|---|---|---|---|
| Modular House - 3D Models, Fertile Soil Productions | https://opengameart.org/content/modular-house-3d-models | CC0 | Walls, windows, doors, steps and railings |
| Modular Buildings, Kenney | https://www.kenney.nl/assets/modular-buildings | CC0 | Stylized building variations |
| City Kit (Suburban), Kenney | https://www.kenney.nl/assets/city-kit-suburban | CC0 | Alternate suburban demo set |

## Imported subset

- Package: Kenney Modular Buildings 2.1
- Downloaded: 2026-08-15
- Archive URL: https://opengameart.org/sites/default/files/kenney_modular-buildings.zip
- Archive SHA-256: `47E1614686B4C0FE55190E88B22FF2B8B10935EDF61F37F478A9B6A99477EDC6`
- Selected upstream GLBs: `building-block`, `building-window`, `building-door`, `roof-gable`
- Conversion: none; the upstream GLB files are retained byte-for-byte
- License copy: `kenney-modular-buildings/License.txt`

Keep the upstream license text beside converted GLB files. If a later asset needs conversion,
record the exact command here.

## Imported material subset

- Asset: ambientCG Bricks 001, 1K JPG
- Source: https://ambientcg.com/view?id=Bricks001
- Downloaded: 2026-08-15
- Archive URL: https://ambientcg.com/get?file=Bricks001_1K-JPG.zip
- Archive SHA-256: `D4E4109F305B7D1094E1C18B2F7F6A3468C62477DE915FF66975FD0155B6873C`
- License: CC0 1.0; official legal text retained as
  `materials/ambientcg-bricks001/LICENSE-CC0-1.0.txt`
- Retained maps: Color, NormalGL and Displacement; 1K JPG files renamed to stable lowercase names
- Retained file SHA-256:
  - `bricks001-color.jpg`: `496F6CFC9C1A7C7750E00927EDE215DE279A2B28833C51E97FDB8F1F71175A0A`
  - `bricks001-normal-gl.jpg`: `E6203542A4C068CFAC844A1A84B07D559B3D3A31C14BD6EB617D549C4DFDC0D5`
  - `bricks001-height.jpg`: `C0F42FED2363799891F4A86DFAD77629BBD20DA3B0BAC167CA4E18281CF01A12`
- Omitted maps: NormalDX, Roughness, USD, Blender, MaterialX, Godot and preview files
- Conversion: none; selected upstream JPG payloads are retained byte-for-byte
