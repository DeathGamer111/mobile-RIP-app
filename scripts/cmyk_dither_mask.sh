#!/bin/bash

set -euo pipefail

if [ $# -ne 5 ]; then
  echo "Usage: $0 input_image output_prn xdpi ydpi working_dir"
  exit 1
fi


INPUT_IMAGE="$1"
OUTPUT_PRN="$2"  # unused for now
XDPI="$3"
YDPI="$4"
WORKDIR="$5"  # this is where all temp output will go

BASENAME=$(basename "$INPUT_IMAGE" | cut -d. -f1)
ASSET_DIR="$(dirname "$0")"  # script location = assetsExtractPath

MAGICK="$ASSET_DIR/magick"
SRGB_PROFILE="$ASSET_DIR/sRGBProfile.icm"
PRINTER_PROFILE="$ASSET_DIR/RIP_App_Plain_Paper.icm"


MASK_C="$ASSET_DIR/mask_512_c.tiff"
MASK_M="$ASSET_DIR/mask_512_m.tiff"
MASK_Y="$ASSET_DIR/mask_512_y.tiff"
MASK_K="$ASSET_DIR/mask_512_k.tiff"


echo "Starting CMYK Dither Pipeline"
echo "Input: $INPUT_IMAGE"
echo "Output PRN: $OUTPUT_PRN"
echo "DPI: ${XDPI}x${YDPI}"
echo "Working dir: $WORKDIR"


# Step 1: Convert to CMYK using ICC
echo "Step 1: Converting to CMYK..."
"$MAGICK" "$INPUT_IMAGE" \
  -profile "$SRGB_PROFILE" \
  -profile "$PRINTER_PROFILE" \
  "${WORKDIR}/${BASENAME}_cmyk.tiff"


# Step 2: Separate into CMYK channels
echo "Step 2: Separating CMYK channels..."
"$MAGICK" "${WORKDIR}/${BASENAME}_cmyk.tiff" -channel C -separate "${WORKDIR}/${BASENAME}_c.tiff"
"$MAGICK" "${WORKDIR}/${BASENAME}_cmyk.tiff" -channel M -separate "${WORKDIR}/${BASENAME}_m.tiff"
"$MAGICK" "${WORKDIR}/${BASENAME}_cmyk.tiff" -channel Y -separate "${WORKDIR}/${BASENAME}_y.tiff"
"$MAGICK" "${WORKDIR}/${BASENAME}_cmyk.tiff" -channel K -separate "${WORKDIR}/${BASENAME}_k.tiff"


# Step 3: Apply blue noise dithering using precomputed per-channel masks
echo "Step 3: Applying blue noise dithering..."

declare -A MASKS=( ["c"]="$MASK_C" ["m"]="$MASK_M" ["y"]="$MASK_Y" ["k"]="$MASK_K" )
declare -A OFFSET_X=( ["c"]=0 ["m"]=64 ["y"]=128 ["k"]=192 )
declare -A OFFSET_Y=( ["c"]=0 ["m"]=64 ["y"]=128 ["k"]=192 )

for CHANNEL in c m y k; do
  echo "...Processing channel: $CHANNEL"

  INPUT="${WORKDIR}/${BASENAME}_${CHANNEL}.tiff"
  OUTPUT="${WORKDIR}/${BASENAME}_${CHANNEL}_1bit.tiff"
  MASK_TMP="${WORKDIR}/mask_${CHANNEL}.tiff"
  MASK_SRC="${MASKS[$CHANNEL]}"

  WIDTH=$("$MAGICK" identify -format "%w" "$INPUT")
  HEIGHT=$("$MAGICK" identify -format "%h" "$INPUT")

  OFFSETX=${OFFSET_X[$CHANNEL]}
  OFFSETY=${OFFSET_Y[$CHANNEL]}

  # Roll and crop mask
  "$MAGICK" "$MASK_SRC" \
    -virtual-pixel tile -roll +${OFFSETX}+${OFFSETY} \
    -crop "${WIDTH}x${HEIGHT}+0+0" +repage \
    "$MASK_TMP"

  # Apply FM screening
  "$MAGICK" "$INPUT" "$MASK_TMP" \
    -fx 'u>=v?1:0' -type bilevel "$OUTPUT"

  # Keep the mask for later dot size analysis
  mv "$MASK_TMP" "${WORKDIR}/${BASENAME}_${CHANNEL}_mask.tiff"
done

echo "Output files ready:"
for CHANNEL in c m y k; do
 echo "  ${WORKDIR}/${BASENAME}_${CHANNEL}_1bit.tiff"
 echo "  ${WORKDIR}/${BASENAME}_${CHANNEL}_mask.tiff"
done
