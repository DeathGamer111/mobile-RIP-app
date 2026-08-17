# Android ImageMagick runtime

This directory contains the minimal ImageMagick runtime used by PrintFlow's
native Android image editor and RIP pipeline. It intentionally excludes the
`magick` command-line program, static libraries, examples, and the duplicate
`libc++_shared.so` shipped in the upstream archives. Qt deploys the C++ runtime
used by the application and these libraries.

## Version and targets

- ImageMagick/Magick++: `7.1.2-29`, Q16 HDRI with OpenMP
- Android minimum API: 24
- Included ABIs: `x86_64` (emulator) and `arm64-v8a` (devices)
- Source/release project: <https://github.com/MolotovCherry/Android-ImageMagick7>

The checked-in headers and Android configuration headers match the published
release. Run `scripts/setup_android_imagemagick.sh` to download, verify, and
stage the four required shared libraries for each ABI.

The normal Android build enables native ImageMagick processing but leaves the
large blue-noise print masks out of quick test APKs. Set
`RIP_EMBED_BLUE_NOISE_MASKS=ON` when producing an APK that must rasterize a
complete print job locally.

## Runtime payload per ABI

- `libmagick++-7.so`
- `libmagickwand-7.so`
- `libmagickcore-7.so`
- `libomp.so`

Little CMS and the image format delegates are linked into MagickCore by the
upstream Android build. PrintFlow therefore uses the matching `lcms2.h` API
without packaging a second LCMS shared library.

ImageMagick is distributed under the ImageMagick License and requires
attribution and a copy of its license in redistributed applications. Before a
production release, the complete third-party notices for the statically linked
delegate libraries must also be included in the application's legal notices.
See <https://imagemagick.org/script/license.php>.
