#!/bin/bash

# === Qt 6 paths ===
export PATH="$HOME/Qt/6.9.0/gcc_64/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/Qt/6.9.0/gcc_64/lib:$LD_LIBRARY_PATH"
export QML_IMPORT_PATH="$HOME/Qt/6.9.0/gcc_64/qml"
export QT_TRANSLATIONS_DIR="$HOME/Qt/6.9.0/gcc_64/translations"


# Variables
APP_NAME="appRIPPrinterApp"
APP_DIR="AppDir"
BUILD_DIR="build/Desktop_Qt_6_9_0-Debug"
ICON_SRC="assets/logo.png"
ICON_DEST="icon.png"
DESKTOP_FILE="$APP_NAME.desktop"

DEPLOY_TOOL="/home/mccalla/Documents/linuxdeployqt/linuxdeployqt/linuxdeployqt"


# === Checks ===
if [[ ! -x "$DEPLOY_TOOL" ]]; then
    echo "❌ Error: linuxdeployqt binary not found at $DEPLOY_TOOL"
    echo "➡️ Build it or update the path to the correct binary"
    exit 1
fi

if [[ ! -f "$BUILD_DIR/$APP_NAME" ]]; then
    echo "❌ Error: Could not find $APP_NAME in $BUILD_DIR"
    exit 1
fi

# === Build AppDir ===
rm -rf $APP_DIR
mkdir -p $APP_DIR/usr

cp "$BUILD_DIR/$APP_NAME" "$APP_DIR/$APP_NAME"
cp "$BUILD_DIR/$APP_NAME" "$APP_DIR/AppRun"  # Optional: make it double-clickable
cp "$ICON_SRC" "$APP_DIR/$ICON_DEST"

# .desktop entry
cat <<EOF > "$APP_DIR/$DESKTOP_FILE"
[Desktop Entry]
Type=Application
Name=RIP App Demo
Exec=$APP_NAME
Icon=icon
Comment=High-precision RIP engine for print workflow demos
Categories=Graphics;Printing;Utility;
Terminal=false
EOF

# === Build the AppImage ===
"$DEPLOY_TOOL" "$APP_DIR/$DESKTOP_FILE" -appimage

# === Done ===
echo "✅ AppImage should now be available in: ./*.AppImage"
