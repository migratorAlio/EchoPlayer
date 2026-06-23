# EchoPlayer

A media player built with **Qt6 + mpv + FFmpeg + SDL2** in C++.

## Architecture

```
EchoPlayer/
├── CMakeLists.txt
├── build.sh
└── src/
    ├── core/
    │   └── audioengine.h / .cpp    FFmpeg decode + SDL2 output + FFT spectrum + LRC lyrics
    ├── ui/
    │   ├── mainwindow.h / .cpp     Frameless main window, control bar, title bar, menus
    │   ├── mpvwidget.h / .cpp      QOpenGLWidget wrapping mpv (zero-copy GPU decode)
    │   ├── controlbar.h / .cpp     Seek slider, transport buttons, volume, time/codec labels
    │   ├── titlebar.h / .cpp       Custom title bar with file menu and window buttons
    │   ├── spectrumwidget.h / .cpp Spectrum visualization (FFT bars)
    │   └── playlistpanel.h / .cpp  Sidebar playlist
    └── main.cpp
```

### mpv-based video rendering (zero-copy GPU decode)
- `MpvWidget` wraps `mpv_handle` + `mpv_render_context` inside a `QOpenGLWidget`
- Loads files with `--audio=no`, renders via `mpv_render_context_render()`
- GPU decode (`--hwdec=cuda` etc.) via right-click menu — no PCIe roundtrip

### AudioEngine (FFmpeg + SDL, separate from mpv)
- Opens the same file independently for audio decode + SDL output
- Spectrum FFT: SDL callback captures PCM, runs FFT, emits `spectrumReady` signal
- LRC lyrics: file-based parsing, auto-detects encoding (UTF-8 / GB18030 / Big5 / Shift-JIS)
- A/V sync: both mpv and AudioEngine seek together on user action

### Playback flow
1. `MainWindow::openMedia(path)` → opens in AudioEngine + mpv
2. mpv fires `fileLoaded` → AudioEngine starts playing
3. User controls (play/pause/seek) operate on both mpv and AudioEngine
4. Right-click → 视频 → 解码 → select backend → `openMedia()` reloads with new `--hwdec`

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Or use the convenience script:

```bash
./build.sh build      # configure + build
./build.sh configure  # cmake only
./build.sh clean      # remove build/
./build.sh run        # open file dialog
./build.sh run file   # play specific file
```

## Features

- **Video**: mpv-based rendering with GPU hardware decode (CUDA, VAAPI, etc.)
- **Audio**: FFmpeg decode + SDL2 output, independent from video pipeline
- **Spectrum**: Real-time FFT visualization with segmented blue bars
- **Lyrics**: LRC file support with auto-encoding detection (UTF-8/GB18030/Big5/Shift-JIS)
- **Poster**: Embedded cover art display for audio-only files
- **Playlist**: Sidebar playlist panel
- **Subtitles**: Multiple subtitle track selection via menu
- **Audio tracks**: Multiple audio track selection
- **Controls**: Play/pause/stop, seek, volume, fullscreen, progress display

## Dependencies

- Qt6 (Core, Gui, Widgets, OpenGLWidgets)
- FFmpeg (libavformat, libavcodec, libavutil, libswresample)
- SDL2
- mpv (>= 0.34, with client library)
- OpenGL
