# TIFF source image admission

The TVE 12.6.0 archive contains 82 TIFF textures: 58 LZW, 19 Deflate and 5
uncompressed. Of these, 73 use unsigned 8-bit samples and 9 use unsigned 16-bit
samples. Thirty-five have three samples and 47 have four; the fourth samples are
tagged as unspecified extra data rather than alpha. Dropping them loses mask data.
The extraction and tag inventory are local diagnostic artifacts under
`build/tve-reference/texture-encoding.json`; source files are not redistributed.

The native asset decoder admits classic TIFF with either byte order, RGB
photometric interpretation, chunky strip layout, 3/4 equally sized unsigned
8/16-bit samples, raw/LZW/Deflate compression, horizontal prediction or no
prediction, and orientations 1 through 4. It preserves unspecified fourth samples
as the fourth output channel; it also accepts explicit unassociated alpha.
Three-sample images receive opaque alpha. Associated alpha, planar/tiled images,
other color spaces, multipage files and BigTIFF are not admitted.

LZW follows TIFF's early-change widths and strip-local clear/end codes. Predictor
restoration runs on complete samples, including 16-bit carries, before conversion.
Sixteen-bit values are rounded to RGBA8 by `(value+128)/257`. No RGB transfer
conversion happens in the TIFF decoder; the existing canonical image cooker
applies the definition's transfer to RGB only. Extra-channel values never undergo
sRGB conversion. This is an RGBA8 pipeline, not lossless 16-bit runtime storage.

These layouts follow [TIFF 6.0](https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf)
and the [Adobe Photoshop TIFF technical notes](https://alternatiff.com/resources/TIFFphotoshop.pdf).

Header, IFD, tag and strip spans are bounded before use; duplicate tags, malformed
counts, truncated streams and excess decoded sizes fail. Allocation is limited by
the supplied output/strip budget. The decoder owns its result, retains no input
pointer and is reentrant. Failed import/cook never publishes partial decoded pixels.
Unknown well-formed metadata tags are ignored, while unsupported pixel semantics
return a checked diagnostic.

The existing `eve.image/2` source definition uses `encoding="tiff"`; the runtime
still receives the existing EVIMG RGBA8 format with `sourceEncoding="tiff"`.
Unity `.tif`/`.tiff` import uses the same decoder and existing metadata color-space
selection. Source normal-map processing settings are still reported unsupported.

Tests cover both byte orders, both bit depths, all three compression families,
fourth-channel retention, predictor restoration, horizontal orientation,
all truncation boundaries of a small LZW file, budget failure and import/cook
round trip. They also run in the renderer-free asset-core profile. All 82 actual
package TIFFs have passed native import admission. This does not cover the 9 TGA
files in the package or complete TVE material conversion.
