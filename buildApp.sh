#!/bin/bash

set -euo pipefail

APP_NAME="RIP_App_Demo"
VERSION="1.0.0"
BUILD_DIR="build"
APPDIR="${APP_NAME}.AppDir"
OUTPUT_DIR="output"
LINUXDEPLOYQT="linuxdeployqt-continuous-x86_64.AppImage"
LINUXDEPLOYQT_URL="https://github.com/probonopd/linuxdeployqt/releases/download/continuous/${LINUXDEPLOYQT}"

echo "🔧 Checking and installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    cmake g++ qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
    qt6-declarative-dev-tools qt6-tools-dev qt6-tools-dev-tools \
    qt6-l10n-tools qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qt-labs-platform \
    qml6-module-qtquick-dialogs qt6-wayland pkg-config \
    liblcms2-dev libcups2-dev libmagick++-6.q16-dev libqt6quick6 \
    wget git fuse patchelf desktop-file-utils libglib2.0-bin \
    libfuse2 zsync xz-utils libgl1-mesa-dev libopengl-dev libvulkan-dev \
    qt6-declarative-dev-tools qt6-qmltooling-plugins \
    qml6-module-qtquick-dialogs libqt6widgets6 qml6-module-qtpositioning \
    qml6-module-qtcore qml6-module-qtquick-window qml-module-qtquick-shapes \
    qt5-qmltooling-plugins qt6-image-formats-plugins libqt6widgets6 \
    libqt6svg6 libqt6svgwidgets6

echo "⬇️  Checking for linuxdeployqt..."
if [[ ! -x "${LINUXDEPLOYQT}" ]]; then
    wget -O "${LINUXDEPLOYQT}" "${LINUXDEPLOYQT_URL}"
    chmod +x "${LINUXDEPLOYQT}"
fi

echo "🏗️  Building the application..."
rm -rf "${BUILD_DIR}" "${APPDIR}" "${OUTPUT_DIR}"
mkdir -p "${BUILD_DIR}" "${OUTPUT_DIR}"
cd "${BUILD_DIR}"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
cd ..

echo "📦 Preparing AppDir..."
mkdir -p "${APPDIR}/usr/bin" \
         "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
         "${APPDIR}/usr/share/${APP_NAME}/assets"

cp "${BUILD_DIR}/appRIPPrinterApp" "${APPDIR}/usr/bin/"
cp rip-app.desktop "${APPDIR}/usr/share/applications/${APP_NAME}.desktop"

if [[ -f "assets/logo.png" ]]; then
    cp assets/logo.png "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
    cp assets/logo.png "${APPDIR}/icon.png"
else
    echo "⚠️  logo.png not found in assets/"
    cp assets/logo.png "${APPDIR}/icon.png" || true
fi

echo "📦 Copying blue noise masks into AppDir..."

mkdir -p "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_256"
cp -r assets/blue_noise_mask_256/* "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_256/"

mkdir -p "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_512"
cp -r assets/blue_noise_mask_512/* "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_512/"


QML_IMPORT_ARG=""
if [[ -d "${APPDIR}/usr/qml" ]]; then
    QML_IMPORT_ARG="-qmlimport=${APPDIR}/usr/qml"
fi

echo "🚀 Running linuxdeployqt..."
./${LINUXDEPLOYQT} \
  "${APPDIR}/usr/share/applications/${APP_NAME}.desktop" \
  -qmake="$(command -v qmake6)" \
  -qmldir="$(pwd)/qml" \
  ${QML_IMPORT_ARG} \
  -appimage \
  -bundle-non-qt-libs \
  -extra-plugins=platforms,styles,iconengines,platformthemes \
  -verbose=2

mv *.AppImage "${OUTPUT_DIR}/"
chmod +x "${OUTPUT_DIR}"/*.AppImage

echo "🧹 Removing old runtime asset cache..."
rm -rf "$HOME/.local/share/${APP_NAME}" || true

echo "✅ Done! AppImage available at ${OUTPUT_DIR}/"

