#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"

# Prefer extracted mode (works without FUSE)
export APPIMAGE_EXTRACT_AND_RUN=1

echo "=== Step 1: Download linuxdeploy ==="
LINUXDEPLOY="$BUILD_DIR/linuxdeploy-x86_64.AppImage"
if [ ! -f "$LINUXDEPLOY" ]; then
    mkdir -p "$BUILD_DIR"
    wget -q -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" -O "$LINUXDEPLOY"
    chmod +x "$LINUXDEPLOY"
fi

# linuxdeploy plugin for Qt
LINUXDEPLOY_QT="$BUILD_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
if [ ! -f "$LINUXDEPLOY_QT" ]; then
    wget -q -c "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" -O "$LINUXDEPLOY_QT"
    chmod +x "$LINUXDEPLOY_QT"
fi



echo "=== Step 2: Build EchoPlayer ==="
cmake -B "$BUILD_DIR/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/build" -j"$(nproc)"

echo "=== Step 3: Create AppDir ==="
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR/build" --prefix /usr

echo "=== Step 4: Bundle with linuxdeploy ==="
export LDAI_OUTPUT="EchoPlayer-x86_64.AppImage"
export QML_SOURCES_PATHS=""
# Include the Wayland platform plugin so the AppImage also runs on Wayland sessions.
export EXTRA_QT_PLUGINS="platforms;wayland-shell-integration;platformthemes"

"$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/EchoPlayer" \
    --executable "$APPDIR/usr/bin/EchoPlayer-playlist" \
    --desktop-file "$APPDIR/usr/share/applications/org.echoplayer.EchoPlayer.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/org.echoplayer.EchoPlayer.png" \
    --plugin qt \
    --output appimage

echo ""
echo "=== Done ==="
echo "AppImage: $BUILD_DIR/EchoPlayer-x86_64.AppImage"
