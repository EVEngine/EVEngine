# Slash mask

`slash-mask.tga` is a 512×128 RGBA alpha mask generated for this example.

- **U** scrolls along the slash arc (tileable ends fade to zero).
- **V** spans the blade width from root (guard) to tip.

Rebuild with:

```sh
python3 examples/sword-slash-trail/tools/generate_mask.py
```

No Pillow required — the tool writes an uncompressed TGA.
Replace the file with any soft white-on-transparent streak to restyle the ribbon.
