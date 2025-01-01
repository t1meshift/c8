#!/usr/bin/env zsh

# A small script to generate a .icns from a single high-res PNG
# Requirements: macOS, sips, iconutil
#
# Usage:
#   ./make_icns.zsh /path/to/highres.png MyIcon
#
#   - /path/to/highres.png is your source icon (e.g., 512x512, 1024x1024).
#   - "MyIcon" is the base name for the .iconset folder and final .icns file.
#
# Example:
#   ./make_icns.zsh my512icon.png MyIcon
#
# This will produce:
#   MyIcon.iconset/   (folder with all scaled PNGs)
#   MyIcon.icns       (final icon file)

# --- 1) Check arguments ---

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <source_icon.png> <icon_name>"
  exit 1
fi

SOURCE_ICON="$1"
ICON_NAME="$2"
ICONSET_DIR="${ICON_NAME}.iconset"

# --- 2) List of desired sizes ---

# Keys: non-Retina (1x) size
# Values: the corresponding Retina (2x) size
# We'll generate both from the same source PNG
typeset -A SIZES=(
  16    32
  32    64
  128   256
  256   512
  512  1024
)

# --- 3) Create (or clean up) the iconset folder ---

rm -rf "$ICONSET_DIR"
mkdir "$ICONSET_DIR"

# --- 4) Use sips to downscale for each size ---

for size1x size2x in "${(@kv)SIZES}"; do
  echo "Generating ${size1x}x${size1x}..."
  sips -z "${size1x}" "${size1x}" "$SOURCE_ICON" --out "$ICONSET_DIR/icon_${size1x}x${size1x}.png" >/dev/null
  
  echo "Generating ${size1x}x${size1x}@2x..."
  sips -z "${size2x}" "${size2x}" "$SOURCE_ICON" --out "$ICONSET_DIR/icon_${size1x}x${size1x}@2x.png" >/dev/null
done

# --- 5) Convert .iconset folder to .icns ---

echo "Converting to .icns..."
iconutil -c icns "$ICONSET_DIR" -o "${ICON_NAME}.icns"

# --- 6) Check result ---

if [ -f "${ICON_NAME}.icns" ]; then
  echo "Success! Generated ${ICON_NAME}.icns"
else
  echo "Error: Failed to create .icns"
  exit 1
fi