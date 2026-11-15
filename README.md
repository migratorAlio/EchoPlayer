# EchoPlayer

A Linux media player built with **Qt6 + mpv render API + FFmpeg** in C++.
Works on both X11 and Wayland sessions.

## Architecture

```
EchoPlayer/
├── CMakeLists.txt
├── build.sh
├── tools/
│   └── update_translations.py    regenerate the three .ts catalogs from tr()
├── packaging/
│   ├── echo-player.desktop / echo-player.metainfo.xml
│   ├── make-appimage.sh
│   └── flatpak/
│       ├── org.echoplayer.EchoPlayer.json   Flatpak manifest (build this)
│       ├── validate.py                      static manifest/metadata checks
│       └── build.sh                         build + install wrapper
└── src/
    ├── core/
    │   ├── audioengine.h/.cpp     FFmpeg metadata + cover + LRC + FFT spectrum (no sound output)
    │   ├── mpriscontroller.h/.cpp MPRIS D-Bus service (media keys, lock screen, metadata)
    │   ├── screensaverinhibitor.h/.cpp  D-Bus Inhibit/UnInhibit while playing
    │   └── types.h                Shared data structures
    ├── ui/
    │   ├── mainwindow.h/.cpp      Frameless window, menus, shortcuts, persistence, tray
    │   ├── mpvwidget.h/.cpp       QOpenGLWidget + mpv_render_context (audio AND video)
    │   ├── adjustpanel.h/.cpp     10-band EQ, subtitle style, video equalizer
    │   ├── thumbnailprovider.h/.cpp  FFmpeg background frame decode for seek preview
    │   ├── controlbar.h/.cpp      Seek / transport / volume / time / codec labels
    │   ├── titlebar.h/.cpp        Custom title bar, file menu, language switch
    │   ├── spectrumwidget.h/.cpp  FFT visualization
    │   └── playlistpanel.h/.cpp   Legacy, not compiled (kept for reference)
    ├── playlist/                  Standalone playlist process + line protocol
    ├── i18n/                      zh_CN / zh_TW / en catalogs + .ts loader
    └── main.cpp                   Single-instance bootstrap (QLocalServer)
```

### Rendering
- `MpvWidget` is a `QOpenGLWidget`. `initializeGL()` creates an
  `mpv_render_context` with the OpenGL backend and passes the native display
  (X11 `Display*` or Wayland `wl_display*`) so hardware decode interop works.
- `paintGL()` renders into the default framebuffer; mpv's update callback
  schedules repaints. No `--wid`, no X11 child windows, no xcb event grabs.

### One playback pipeline
mpv owns both audio and video. The old design used `--audio=no` plus a separate
FFmpeg+SDL audio engine and a 5-second polling A/V correction loop — that caused
audible jumps and made speed changes / subtitle delays nearly impossible. Now
`AudioEngine` is analysis-only: metadata, cover art, LRC lyrics and a real FFT
spectrum from a silent PCM decode. No A/V sync code is needed. Videos and
network streams skip the analysis decode entirely.

### Playback flow
1. `MainWindow::openMedia(path)` → AudioEngine reads metadata for local files,
   mpv loads the file (or network URL)
2. mpv `fileLoaded` → apply saved settings (including the whole adjustment
   panel), restore last position, start spectrum, populate audio/subtitle menus
   from mpv `track-list`, publish MPRIS metadata
3. mpv `playbackFinished` (EOF) → repeat-one / shuffle / playlist loop / next file
4. Position / duration / pause come from mpv property observations; MPRIS status
   and screen-saver inhibition follow the pause state

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Or use the convenience script:

```bash
./build.sh build       # configure + build
./build.sh configure   # cmake only
./build.sh clean       # remove build/
./build.sh run         # open file dialog
./build.sh run file    # play specific file
./build.sh install     # install to system
./build.sh uninstall   # uninstall from system
```

## Packaging

### deb
```bash
./build.sh deb
# → build/echo-player-1.0.0_amd64.deb
```
Install: `sudo dpkg -i build/echo-player-1.0.0_amd64.deb`

Installs the desktop entry (`org.echoplayer.EchoPlayer.desktop`), AppStream
metadata, the `org.echoplayer.EchoPlayer.png` icon and all three translations.

### AppImage
Requires `wget` and `fuse`.
```bash
./build.sh appimage
# → build-appimage/EchoPlayer-x86_64.AppImage
```

### Flatpak
`packaging/flatpak/org.echoplayer.EchoPlayer.json` is the manifest to build.
It uses the KDE 6.8 Platform/SDK (Qt6), the freedesktop ffmpeg-full SDK
extension for the FFmpeg libraries, and builds **mpv v0.36.0** as libmpv only.
That mpv version can disable libplacebo, which removes the long
libplacebo/Vulkan/shaderc dependency chain that newer mpv releases require.
Every `-D` option in the manifest is checked against mpv v0.36.0's real
`meson_options.txt` by `validate.py`.

Prerequisites (once):
```bash
sudo apt install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install -y flathub org.kde.Platform//6.8 org.kde.Sdk//6.8 \
    org.freedesktop.Sdk.Extension.ffmpeg-full//24.08
```

Build, install and run:
```bash
./packaging/flatpak/build.sh
flatpak run org.echoplayer.EchoPlayer /path/to/video.mp4
```

`build.sh` runs the static checks, asks `flatpak-builder --show-manifest` to
parse the manifest, verifies the runtimes, checks the SDK for `libass` /
`libpulse`, then builds. You can run the checks alone on any machine:

```bash
python3 packaging/flatpak/validate.py
```

**Verification status:** the 29 static checks pass on the development machine,
but `flatpak`, `flatpak-builder` and `appstreamcli` are not installed here and
installing them needs a password, so a real sandbox build has **not** been run.
deb / AppImage / native builds are verified. If the SDK lacks `libass`, add a
libass source module before mpv; the build script prints that warning.

## Fine-tuning panel (`Ctrl+P`)

A non-modal dialog with three tabs. Every control writes straight to an mpv
property, takes effect immediately, and is saved under `QSettings` `adjust/*`.

| Tab | Controls | mpv property |
|---|---|---|
| Audio | 10-band graphic EQ (31 Hz … 16 kHz, ±12 dB), 4 presets, volume normalization, channel balance, audio delay | `af` (`dynaudnorm` + `equalizer` chain), `balance`, `audio-delay` |
| Subtitles | size, delay, vertical position, font, bold/italic, horizontal align, outline width/colour, shadow offset/colour, edge blur, three colour pickers | `sub-scale`, `sub-delay`, `sub-pos`, `sub-font`, `sub-bold`, `sub-italic`, `sub-align-x`, `sub-border-*`, `sub-shadow-*`, `sub-blur`, `sub-color` |
| Picture | brightness, contrast, saturation, gamma, hue | the matching video equalizer properties |

The EQ is the single owner of mpv's `af` property: the right-click sound
presets and the `E` shortcut simply drive the same band configuration, so the
menu and the panel can never fight over the filter chain. All values are
re-applied whenever a new file loads.

## Translations

Three complete catalogs: 简体中文 (source), 繁體中文 and English. Unlike a
normal Qt app, `TsTranslator` reads `.ts` XML directly — no `lrelease` step.
Switching language in the title bar re-translates the whole UI **immediately**:
menus, tray, OSD strings, the adjustment panel, and the separate playlist
process (the player forwards a `LANG` message over the line protocol).

After adding a new `tr("...")`, regenerate the catalogs:

```bash
python3 tools/update_translations.py
```

The script scans every `tr()` in the sources, fails if any string lacks an
English translation, and writes all three `.ts` files. Add the new wording to
the `EN` / `TW_WORDS` dictionaries in that script.

## Features

- **Video**: mpv render API with GPU hardware decode (auto / CUDA / VAAPI / VDPAU / Vulkan)
- **Audio**: mpv handles all sound output; audio track switching, channel balance,
  10-band equalizer + 4 presets, volume normalization, audio delay
- **Subtitles**: internal + external tracks, drag-to-load srt/ass/vtt, delay ±,
  font/size/position/colours/outline/shadow/blur, track switching
- **Speed**: 0.25x – 4x, frame stepping, A/B loop
- **Picture**: aspect ratio, rotation, brightness / contrast / saturation / gamma / hue
- **Screenshot**: saved to `Pictures/EchoPlayer` with one key
- **Spectrum**: real-time FFT visualization for audio-only files
- **Lyrics**: LRC with auto encoding detection (UTF-8 / GB18030 / Big5 / Shift-JIS)
- **Cover art**: embedded poster for audio-only files, also exported as MPRIS
  `mpris:artUrl` so the lock screen can show it
- **Playlist**: sidebar process with add / remove / reorder / clear, drag & drop
  appending, shuffle, three repeat modes (off / playlist / one), persistence,
  CUE sheet track splitting and auto-next on EOF
- **Network streams**: `Ctrl+U` or paste a URL; http/https/rtsp/rtmp/udp/srt/ftp
  are played by mpv without a blocking FFmpeg probe
- **Seek preview**: hovering the progress bar shows a decoded video frame + time
  (local files only; 5-second buckets, cached)
- **Desktop integration**: MPRIS media keys and desktop playback controls,
  lock-screen metadata/cover, screen-saver inhibition while playing, system tray
  (close-to-tray configurable), single instance, file association, AppStream metadata
- **Resume**: remembers where you stopped for each file
- **Settings**: volume, speed, hwdec, language, window geometry, recent files,
  repeat/shuffle, tray preference, every adjustment-panel value
- **OSD**: on-screen feedback for volume, speed, seeking, subtitles, etc.
- **Languages**: 简体中文 / 繁體中文 / English, all 192 UI strings translated

### Key bindings

| Key | Action | Key | Action |
|---|---|---|---|
| `Space` / `K` | Play / pause | `S` | Screenshot |
| `←` / `→` | ±5 seconds | `PgUp` / `PgDn` | Previous / next file |
| `Shift+←` / `→` | ±30 seconds | `Z` / `X` | Subtitle delay ∓0.1s |
| `↑` / `↓` | Volume ±5% | `A` | Set A/B loop point |
| `M` | Mute | `I` | Media info |
| `F` / `Enter` | Fullscreen | `1`-`4` | Aspect ratio |
| `Esc` | Exit fullscreen | `R` | Rotate 90° |
| `[` / `]` | Speed −/+ 0.1 | `B` / `N` | Brightness +/- |
| `Backspace` | Reset to 1.0x | `E` | Cycle audio preset |
| `,` / `.` | Frame back / forward | `L` | Cycle repeat mode |
| `Ctrl+O` | Open file | `Ctrl+U` | Open network stream |
| `Ctrl+P` | Fine-tuning panel | `Ctrl+R` | Toggle shuffle |
| `Ctrl+Q` | Quit | `H` | Hide to tray |

Right-click the video for the full menu (tracks, decoder, aspect, subtitles,
playback, tools). Fullscreen reveals the title/control bars when the pointer
touches the screen edges.

## Dependencies

- Qt6 (Core, Gui, Widgets, OpenGLWidgets, Network, DBus) >= 6.5
- FFmpeg (libavformat, libavcodec, libavutil, libswscale, libswresample)
- mpv (>= 0.34, with client + render API; the Flatpak pins v0.36.0)
- OpenGL 3.3 capable driver

SDL2 and X11/xcb are **not** required.
