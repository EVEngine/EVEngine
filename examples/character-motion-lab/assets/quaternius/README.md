# Quaternius CC0 assets

Downloaded 2026-09-09 from the author's free distributions:

- Universal Animation Library Standard, published by Quaternius on
  https://opengameart.org/content/universal-animation-library
  Download: https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip
  Archive SHA-256: `18ff1a7215f4852b320203e8aaf02a1578b5c8eef9027fbaedfcedc7b85a3ac2`.
- Universal Base Characters Standard:
  https://quaternius.itch.io/universal-base-characters
  Product: https://quaternius.com/packs/universalbasecharacters.html
  Free itch.io upload 15861669, `Universal Base Characters[Standard].zip`.
  Archive SHA-256: `fdbf1804c90dfc1ea03e992bff7da2dfd1a79318e13270a660180f9308455f40`.

Both are CC0 1.0. Original author license notices are included as
LICENSE-ANIMATIONS.txt and LICENSE-CHARACTERS.txt. No paid Source-tier models
are included. This free character archive contains two bodies, not all six
advertised across the complete pack.

Selected files: Godot AnimationLibrary_Godot_Standard.glb (renamed to
AnimationLibrary.glb, otherwise unmodified), the two Godot-UE FullBody glTF
characters and their referenced buffers/textures. The library has 46 animation
entries including A_TPose; the scene exposes eight representative performances.

Import corrections: the author's glTF references missing `T_Eye_Normal_png.png`
and `T_Hair_1_Normal_png.png`; these two URIs are corrected to the corresponding
existing `_Normal.png` files. JSON is pretty-printed. License notice text is preserved with normalized LF
line endings and trailing whitespace removed. Geometry, bind poses,
animation data and texture pixels are unchanged. sha256.json records every
bundled payload and license file after these corrections.
