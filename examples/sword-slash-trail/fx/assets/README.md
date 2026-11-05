# Slash masks

## Procedural

`slash-mask.tga` is a 512×128 RGBA alpha mask generated for this example.

- **U** scrolls along the slash arc (tileable ends fade to zero).
- **V** spans the blade width from root (guard) to tip.

```sh
python3 examples/sword-slash-trail/tools/generate_mask.py
```

## Vendor (CC0, downloaded)

See [`vendor/LICENSE.md`](vendor/LICENSE.md). Default runtime style is
`vendor/oga-slash-blue.png` (MetaShinryu / OpenGameArt, CC0 crescent).
