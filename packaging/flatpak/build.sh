#!/usr/bin/env bash
# Build and install the Flatpak locally:
#   ./packaging/flatpak/build.sh
# Override the build dir with ECHOPLAYER_FLATPAK_BUILD=/some/path.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MANIFEST="$HERE/org.echoplayer.EchoPlayer.json"
BUILD_DIR="${ECHOPLAYER_FLATPAK_BUILD:-$ROOT/build-flatpak}"
APP_ID="org.echoplayer.EchoPlayer"
RUNTIME_VERSION="6.8"
FFMPEG_VERSION="24.08"

say() { printf '\n==> %s\n' "$*"; }

say "静态校验 manifest 与桌面元数据"
python3 "$HERE/validate.py"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    cat >&2 <<'EOF'
缺少 flatpak-builder，无法真正构建。请先安装并添加 flathub：
  sudo apt install flatpak flatpak-builder
  flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
EOF
    exit 1
fi

say "让 flatpak-builder 解析一次 manifest（不下载 runtime、不编译）"
flatpak-builder --show-manifest "$MANIFEST" >/dev/null

say "检查 runtime / SDK / FFmpeg 扩展"
missing=0
for ref in "org.kde.Platform//$RUNTIME_VERSION" "org.kde.Sdk//$RUNTIME_VERSION"; do
    if ! flatpak info "$ref" >/dev/null 2>&1; then
        echo "缺少 $ref ；安装命令： flatpak install -y flathub $ref" >&2
        missing=1
    fi
done
if ! flatpak list --runtime --columns=ref 2>/dev/null | grep -q ffmpeg-full; then
    echo "缺少 FFmpeg 扩展；安装命令：" >&2
    echo "  flatpak install -y flathub org.freedesktop.Sdk.Extension.ffmpeg-full//$FFMPEG_VERSION" >&2
    missing=1
fi
[ "$missing" -eq 0 ] || exit 1

say "检查 SDK 内 mpv 的必需依赖（libass / libpulse）"
if ! flatpak run --command=sh "org.kde.Sdk//$RUNTIME_VERSION" -c \
        'pkg-config --exists libass && pkg-config --exists libpulse' 2>/dev/null; then
    echo "警告：SDK 内缺少 libass 或 libpulse，构建可能失败。" >&2
    echo "  libass 缺失  -> 在 manifest 的 mpv 模块之前加一个 libass 源码模块" >&2
    echo "  libpulse 缺失 -> 换用带 libpulse 的 SDK（去掉 -Dpulse=enabled 会没声音）" >&2
fi

if command -v appstreamcli >/dev/null 2>&1; then
    say "校验 AppStream 元数据"
    appstreamcli validate --no-net "$ROOT/packaging/echo-player.metainfo.xml" || true
fi

say "构建并安装到当前用户"
flatpak-builder --user --install --force-clean "$BUILD_DIR" "$MANIFEST"

say "完成"
echo "运行： flatpak run $APP_ID /path/to/video.mp4"
