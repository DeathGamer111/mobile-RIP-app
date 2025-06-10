#!/bin/bash

# Check if input image is provided
if [ $# -ne 1 ]; then
  echo "Usage: $0 input_image"
  exit 1
fi

INPUT_IMAGE="$1"
BASENAME=$(basename "$INPUT_IMAGE" | cut -d. -f1)

export MAGICK_CONFIGURE_PATH=/home/mccalla/Downloads/
source ~/.bashrc

echo "🔧 Starting CMYK Dither Pipeline"
echo "Input image: $INPUT_IMAGE"
echo "Base name: $BASENAME"

# Step 1: Convert sRGB to CMYK using ICC profiles
echo "Step 1: Converting to CMYK using ICC profiles..."
./magick "$INPUT_IMAGE" \
  -profile /home/mccalla/Documents/sRGBProfile.icm \
  -profile /home/mccalla/Documents/RIP_App_Plain_Paper.icm \
  "${BASENAME}_cmyk.tiff"

# Step 2: Separate into CMYK channels
echo "Step 2: Separating CMYK channels..."
./magick "${BASENAME}_cmyk.tiff" -channel C -separate "${BASENAME}_c.tiff"
./magick "${BASENAME}_cmyk.tiff" -channel M -separate "${BASENAME}_m.tiff"
./magick "${BASENAME}_cmyk.tiff" -channel Y -separate "${BASENAME}_y.tiff"
./magick "${BASENAME}_cmyk.tiff" -channel K -separate "${BASENAME}_k.tiff"

# Step 3: Apply blue noise dithering using precomputed per-channel masks
echo "Step 3: Applying blue noise dithering using precomputed per-channel masks..."

# Paths to precomputed large masks
declare -A CHANNEL_MASKS=(
  ["c"]="/home/mccalla/Downloads/precomputed_masks/512/mask_c.tiff"
  ["m"]="/home/mccalla/Downloads/precomputed_masks/512/mask_m.tiff"
  ["y"]="/home/mccalla/Downloads/precomputed_masks/512/mask_y.tiff"
  ["k"]="/home/mccalla/Downloads/precomputed_masks/512/mask_k.tiff"
)

# Per-channel Moiré offset
declare -A CHANNEL_OFFSETS_X=( ["c"]=0 ["m"]=64 ["y"]=128 ["k"]=192 )
declare -A CHANNEL_OFFSETS_Y=( ["c"]=0 ["m"]=64 ["y"]=128 ["k"]=192 )

for CHANNEL in c m y k; do
  echo "  → Processing $CHANNEL channel..."

  INPUT_CHANNEL="${BASENAME}_${CHANNEL}.tiff"
  OUTPUT_1BIT="${BASENAME}_${CHANNEL}_1bit.tiff"
  MASK_OUTPUT="${BASENAME}_${CHANNEL}_mask.tiff"

  WIDTH=$(identify -format "%w" "$INPUT_CHANNEL")
  HEIGHT=$(identify -format "%h" "$INPUT_CHANNEL")

  OFFSET_X=${CHANNEL_OFFSETS_X[$CHANNEL]}
  OFFSET_Y=${CHANNEL_OFFSETS_Y[$CHANNEL]}
  PRE_MASK=${CHANNEL_MASKS[$CHANNEL]}

  # Build rolled + cropped mask
  ./magick "$PRE_MASK" \
    -virtual-pixel tile -roll +${OFFSET_X}+${OFFSET_Y} \
    -crop "${WIDTH}x${HEIGHT}+0+0" +repage \
    "mask_${CHANNEL}.tiff"

  # Apply FX thresholding
  ./magick "$INPUT_CHANNEL" "mask_${CHANNEL}.tiff" \
    -fx 'u>=v?1:0' -type bilevel "$OUTPUT_1BIT"

  # Keep mask for dot size reference
  mv "mask_${CHANNEL}.tiff" "$MASK_OUTPUT"
done

echo "✅ Dithered 1-bit CMYK files created:"
echo "  ${BASENAME}_c_1bit.tiff"
echo "  ${BASENAME}_m_1bit.tiff"
echo "  ${BASENAME}_y_1bit.tiff"
echo "  ${BASENAME}_k_1bit.tiff"

# Step 4: Generate the PRN
echo "Step 4: Generating PRN file with resolution 720x720..."
mccall-venv/bin/python3 generate_prn_1bit_unpacked.py \
${BASENAME}_c_1bit.tiff \
${BASENAME}_m_1bit.tiff \
${BASENAME}_y_1bit.tiff \
${BASENAME}_k_1bit.tiff \
${BASENAME}_c_mask.tiff \
${BASENAME}_m_mask.tiff \
${BASENAME}_y_mask.tiff \
${BASENAME}_k_mask.tiff \
720 \
720 \
${BASENAME}_.prn

# Step 5: Cleanup
echo "Step 5: Removing intermediary images..."
rm ${BASENAME}_c_1bit.tiff
rm ${BASENAME}_m_1bit.tiff
rm ${BASENAME}_y_1bit.tiff
rm ${BASENAME}_k_1bit.tiff
rm ${BASENAME}_c.tiff
rm ${BASENAME}_m.tiff
rm ${BASENAME}_y.tiff
rm ${BASENAME}_k.tiff
rm ${BASENAME}_cmyk.tiff
rm ${BASENAME}_c_mask.tiff
rm ${BASENAME}_m_mask.tiff
rm ${BASENAME}_y_mask.tiff
rm ${BASENAME}_k_mask.tiff

