#!/bin/bash

# Name of the Docker image
IMAGE_NAME="rip-builder"
CONTAINER_NAME="rip-builder-container"
DEPLOYQT_HOST_PATH="/home/mccalla/Documents/squashfs-root"

# Clean old build artifacts
sudo rm -rf build AppDir output
mkdir -p output AppDir/usr/bin AppDir/usr/qml

# Build Docker image
docker build -t $IMAGE_NAME .

# Run build and AppImage packaging inside container
docker run --rm \
    --name $CONTAINER_NAME \
    -v "$(pwd)":/app \
    -v "$DEPLOYQT_HOST_PATH":/linuxdeployqt-extracted \
    -w /app \
    $IMAGE_NAME \
    bash -c "
        set -e
        export VERSION=1.0.0 && \
        mkdir -p build && cd build && \
        cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && \
        make -j\$(nproc) && make install && \
        cp appRIPPrinterApp ../AppDir/usr/bin/ && \
        cd .. && mkdir -p AppDir/usr/lib/ && \
        cp -r /usr/lib/x86_64-linux-gnu/qt6/qml/* AppDir/usr/qml/ && \
        cp rip-app.desktop AppDir/appRIPPrinterApp.desktop && \
        cp /usr/lib/x86_64-linux-gnu/libQt6Quick.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6Qml.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6Gui.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6Core.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6Widgets.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6Network.so.6 AppDir/usr/lib/ && \
	cp /usr/lib/x86_64-linux-gnu/libQt6QuickLayouts.so.6 AppDir/usr/lib/ && \
        cp assets/logo.png AppDir/icon.png && \
        chmod -R 777 AppDir && \
        echo 'Running linuxdeployqt...' && \
	QML_SOURCES_PATHS=/app/qml \
        QML_IMPORT_PATH=/app/AppDir/usr/qml \
        QML2_IMPORT_PATH=/app/AppDir/usr/qml \
        QTDIR=/usr \
	QMAKE=/usr/bin/qmake6 /linuxdeployqt-extracted/AppRun AppDir/appRIPPrinterApp.desktop \
	    -appimage \
	    -qmldir=/app/qml \
	    -qmlimport=/app/AppDir/usr/qml \
	    -executable=/app/AppDir/usr/bin/appRIPPrinterApp \
	    -extra-plugins=platforms \
	    -bundle-non-qt-libs \
	    -verbose=2 && \
	mkdir -p AppDir/usr/qml && \
	find AppDir/usr -type f \\( -name \"*.so\" -o -name \"*.so.*\" \\) | while read sofile; do \
            echo \"🔧 Patching RPATH in \$sofile\" && \
            patchelf --set-rpath '\$ORIGIN/../lib:\$ORIGIN/../../lib' \"\$sofile\" ; \
        done && \
        patchelf --set-rpath '$ORIGIN/../lib' AppDir/usr/bin/appRIPPrinterApp && \
	mv *.AppImage output/
    "

# Ensure build artifacts are writable on host
sudo chmod -R 777 build AppDir output

# Check result
if compgen -G "output/*.AppImage" > /dev/null; then
    echo "✅ AppImage successfully built in ./output/"
else
    echo "❌ AppImage build failed. Check logs."
fi

output/*.AppImage --appimage-extract
ldd squashfs-root/usr/bin/appRIPPrinterApp | grep "not found"
ldd squashfs-root/usr/qml/QtQuick/Layouts/libqquicklayoutsplugin.so | grep "not found"
find squashfs-root/ -name "libQt6QuickLayouts.so*"


