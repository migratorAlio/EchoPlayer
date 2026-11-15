#!/usr/bin/env python3
"""Static validation for the Flatpak manifest and its desktop metadata.

Run this before (or instead of) a real build:

    python3 packaging/flatpak/validate.py

It checks everything that can fail before flatpak-builder starts: JSON shape,
app-id/desktop/metainfo/icon naming, sandbox permissions, meson option names
against mpv v0.36.0's real meson_options.txt, source paths, translation files
and the install rules in CMakeLists.txt. It does NOT replace a real build —
use build.sh for that (needs flatpak-builder + the KDE SDK runtimes).
"""

from pathlib import Path
import json
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "org.echoplayer.EchoPlayer.json"
APP_ID = "org.echoplayer.EchoPlayer"

# Option names from mpv v0.36.0 meson_options.txt. Passing an unknown -Dfoo
# makes meson abort, which is the most common cause of a broken mpv module.
MPV_036_OPTIONS = {
    "gpl", "cplayer", "libmpv", "build-date", "tests", "ta-leak-report",
    "cdda", "cplugins", "dvbin", "dvdnav", "iconv", "javascript", "lcms2",
    "libarchive", "libavdevice", "libbluray", "lua", "pthread-debug",
    "rubberband", "sdl2", "sdl2-gamepad", "stdatomic", "uchardet", "uwp",
    "vapoursynth", "vector", "win32-internal-pthreads", "zimg", "zlib",
    "alsa", "audiounit", "coreaudio", "jack", "openal", "opensles",
    "oss-audio", "pipewire", "pulse", "sdl2-audio", "sndio", "wasapi",
    "caca", "cocoa", "d3d11", "direct3d", "dmabuf-wayland", "drm", "egl",
    "egl-android", "egl-angle", "egl-angle-lib", "egl-angle-win32", "egl-drm",
    "egl-wayland", "egl-x11", "gbm", "gl", "gl-cocoa", "gl-dxinterop",
    "gl-win32", "gl-x11", "jpeg", "libplacebo", "libplacebo-next", "rpi",
    "sdl2-video", "shaderc", "sixel", "spirv-cross", "plain-gl", "vdpau",
    "vdpau-gl-x11", "vaapi", "vaapi-drm", "vaapi-wayland", "vaapi-x11",
    "vaapi-x-egl", "vulkan", "wayland", "x11", "xv",
    "android-media-ndk", "cuda-hwaccel", "cuda-interop", "d3d-hwaccel",
    "d3d9-hwaccel", "gl-dxinterop-d3d9", "ios-gl", "rpi-mmal",
    "videotoolbox-gl", "vulkan-interop",
    "macos-10-11-features", "macos-10-12-2-features", "macos-10-14-features",
    "macos-cocoa-cb", "macos-media-player", "macos-touchbar", "swift-build",
    "swift-flags", "html-build", "manpage-build", "pdf-build",
}

REQUIRED_FINISH_ARGS = {
    "--share=ipc",
    "--socket=wayland",
    "--socket=pulseaudio",
    "--share=network",
    "--device=dri",
    "--own-name=org.mpris.MediaPlayer2.EchoPlayer",
    "--talk-name=org.freedesktop.ScreenSaver",
}


class Report:
    def __init__(self):
        self.failures = []
        self.checks = 0

    def check(self, condition, label, detail=""):
        self.checks += 1
        if condition:
            print(f"  PASS  {label}")
        else:
            msg = f"{label}" + (f": {detail}" if detail else "")
            print(f"  FAIL  {msg}")
            self.failures.append(msg)

    def done(self):
        print(f"\n{self.checks - len(self.failures)}/{self.checks} checks passed")
        return 1 if self.failures else 0


def main():
    r = Report()
    print("== manifest ==")
    try:
        m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        print(f"  FAIL  cannot read manifest: {e}")
        return 1

    r.check(m.get("app-id") == APP_ID, "app-id matches desktop/metainfo")
    r.check(bool(m.get("runtime")) and bool(m.get("sdk")), "runtime and sdk declared")
    r.check(m.get("command") == "EchoPlayer", "command points at the built binary")
    r.check("org.freedesktop.Sdk.Extension.ffmpeg-full" in m.get("sdk-extensions", []),
            "FFmpeg SDK extension declared")
    ext = m.get("add-extensions", {}).get("org.freedesktop.Platform.ffmpeg-full", {})
    r.check(ext.get("version") == "24.08" and ext.get("add-ld-path") == "lib",
            "ffmpeg-full extension version matches the freedesktop 24.08 base",
            str(ext))
    bo = m.get("build-options", {})
    r.check(all(k in bo for k in ("append-path", "prepend-ld-library-path",
                                  "append-pkg-config-path")),
            "FFmpeg pkg-config / ld paths wired into build-options")

    print("== sandbox ==")
    args = set(m.get("finish-args", []))
    missing = sorted(REQUIRED_FINISH_ARGS - args)
    r.check(not missing, "required sandbox permissions present", ", ".join(missing))
    r.check(any(a.startswith("--filesystem=home") or a.startswith("--filesystem=xdg-videos")
                for a in args), "media files are reachable from the sandbox")

    print("== modules ==")
    mods = m.get("modules", [])
    names = [x.get("name") for x in mods]
    r.check(names == ["mpv", "EchoPlayer"], "module order: mpv before EchoPlayer", str(names))
    r.check("*.pc" not in m.get("cleanup", []) and
            "/lib/pkgconfig" not in m.get("cleanup", []),
            "cleanup keeps mpv.pc (EchoPlayer's pkg_check_modules needs it)")

    mpv = next((x for x in mods if x.get("name") == "mpv"), {})
    opts = mpv.get("config-opts", [])
    bad = [o for o in opts if (mm := re.match(r"-D([\w-]+)=", o))
           and mm.group(1) not in MPV_036_OPTIONS]
    r.check(not bad, "all meson options exist in mpv v0.36.0", ", ".join(bad))
    r.check(any(o == "-Dlibmpv=true" for o in opts) and
            any(o == "-Dcplayer=false" for o in opts),
            "builds libmpv only (no CLI player)")
    r.check(any(o == "-Dlibplacebo=disabled" for o in opts),
            "libplacebo disabled (0.36 keeps the legacy GL renderer, no extra deps)")
    r.check(any(o == "-Dpulse=enabled" for o in opts),
            "PulseAudio output enabled (otherwise the Flatpak is silent)")
    src = (mpv.get("sources") or [{}])[0]
    r.check(src.get("tag") == "v0.36.0" and src.get("type") == "git",
            "mpv source pinned to the version whose options were verified")

    app = next((x for x in mods if x.get("name") == "EchoPlayer"), {})
    r.check(app.get("buildsystem") == "cmake-ninja" and app.get("builddir") is True,
            "out-of-tree CMake build (host build/ dir cannot leak into the sandbox)")
    app_src = (app.get("sources") or [{}])[0]
    src_dir = (HERE / app_src.get("path", "")).resolve()
    r.check(app_src.get("type") == "dir" and (src_dir / "CMakeLists.txt").is_file(),
            "dir source points at the project root", str(src_dir))

    print("== desktop integration ==")
    desktop = (ROOT / "packaging/echo-player.desktop").read_text(encoding="utf-8")
    r.check(f"Icon={APP_ID}" in desktop, "desktop Icon matches app-id")
    r.check("Exec=EchoPlayer %F" in desktop, "desktop Exec matches command")
    r.check("StartupWMClass=EchoPlayer" in desktop, "StartupWMClass set")

    meta = ET.parse(ROOT / "packaging/echo-player.metainfo.xml").getroot()
    r.check(meta.findtext("id") == APP_ID, "AppStream <id> matches app-id")
    r.check(meta.find("launchable") is not None and
            meta.find("launchable").text == f"{APP_ID}.desktop" and
            meta.find("launchable").get("type") == "desktop-id",
            "AppStream launchable matches installed desktop name")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    r.check(f"RENAME {APP_ID}.png" in cmake, "icon installed under app-id name")
    r.check(f"RENAME {APP_ID}.desktop" in cmake, "desktop installed under app-id name")
    r.check(f"RENAME {APP_ID}.metainfo.xml" in cmake,
            "metainfo installed under app-id name")
    r.check("share/EchoPlayer/translations" in cmake,
            "translations installed where LanguageManager looks for them")
    r.check("Qt6::DBus" in cmake and "Qt6::Network" in cmake,
            "MPRIS / single-instance Qt modules linked")

    print("== translations ==")
    counts = []
    for loc in ("en", "zh_CN", "zh_TW"):
        p = ROOT / f"src/i18n/echoPlayer_{loc}.ts"
        try:
            root = ET.parse(p).getroot()
            counts.append((loc, root.get("language"),
                           sum(len(c.findall("message")) for c in root.findall("context"))))
        except (OSError, ET.ParseError):
            counts.append((loc, None, -1))
    r.check(all(loc == lang and n > 0 for loc, lang, n in counts),
            "three .ts files parse and declare their own language", str(counts))
    r.check(len({n for _, _, n in counts}) == 1,
            "all three catalogs carry the same number of messages", str(counts))

    print("== build prerequisites (informational) ==")
    import shutil
    for tool in ("flatpak", "flatpak-builder", "appstreamcli"):
        print(f"  {tool}: {'available' if shutil.which(tool) else 'NOT INSTALLED'}")
    print("  A real build needs: flatpak-builder, org.kde.Platform//6.8,")
    print("  org.kde.Sdk//6.8 and ffmpeg-full//24.08. See build.sh.")
    return r.done()


if __name__ == "__main__":
    sys.exit(main())
