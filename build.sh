#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

case "${1:-build}" in
    configure)
        cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        ;;
    build)
        cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        cmake --build "$BUILD_DIR" -j"$(nproc)"
        mkdir -p "$BUILD_DIR/translations"
        for f in "$ROOT_DIR/src/i18n/"*.ts; do
            base="$(basename "$f")"
            # If the source filename starts with lowercase "echoPlayer_", rename to "EchoPlayer_"
            newbase="${base/#echoPlayer_/EchoPlayer_}"
            cp "$f" "$BUILD_DIR/translations/$newbase"
        done
        ;;
    run-playlist)
        exec "$BUILD_DIR/EchoPlayer-playlist"
        ;;
    run)
        if [ -n "$2" ]; then
            exec "$BUILD_DIR/EchoPlayer" "$2"
        else
            exec "$BUILD_DIR/EchoPlayer"
        fi
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        ;;
    install)
        cmake --build "$BUILD_DIR" --target install
        ;;
    uninstall)
        cmake --build "$BUILD_DIR" --target uninstall 2>/dev/null || \
            xargs rm -f < "$BUILD_DIR/install_manifest.txt" 2>/dev/null || \
            echo "Nothing to uninstall."
        ;;
    deb)
        cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$BUILD_DIR" -j"$(nproc)"
        cd "$BUILD_DIR" && cpack -G DEB
        echo "Package: $BUILD_DIR/echo-player-*.deb"
        ;;
    appimage)
        exec ./packaging/make-appimage.sh
        ;;
    *)
        echo "Usage: $0 [configure|build|run|install|uninstall|clean|deb|appimage] [file]"
        exit 1
        ;;
esac
