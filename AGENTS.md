## Architecture

### mpv-based video rendering (zero-copy GPU decode)
- `MpvWidget` (QOpenGLWidget): wraps `mpv_handle` + `mpv_render_context`
- Loads files with `--audio=no`, renders via `mpv_render_context_render()`
- mpv manages HW decode internally: `--hwdec=cuda` etc. via right-click menu
- No PCIe roundtrip — GPU decode → GPU render via CUDA-OpenGL interop

### AudioEngine (FFmpeg + SDL, separate from mpv)
- Opens the same file independently for audio decode + SDL output
- Spectrum FFT: SDL callback captures PCM, FFT → `spectrumReady` signal
- LRC lyrics: file-based parsing, synced to audio position
- A/V sync: both mpv and AudioEngine seek together on user action

### Playback flow
1. `MainWindow::openMedia(path)` → opens in AudioEngine + mpv
2. mpv fires `fileLoaded` → AudioEngine starts playing
3. User controls (play/pause/seek) operate on both mpv and AudioEngine
4. Right-click → 视频 → 解码 → select backend → `openMedia()` reloads with new `--hwdec`

## Build & Run
- `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)`
- `./build.sh run [file]`
- Depends on: Qt6 (Core Gui Widgets OpenGLWidgets), FFmpeg (avformat avcodec avutil swresample), SDL2, mpv (>= 0.34)

## Remaining Gaps
- **Spectrum**: AudioEngine has FFT code but needs the `triggerSpectrumUpdate()` timer to be properly driven by position updates from mpv (currently called from `onSpectrumTimer` which fires every 30ms but may not trigger at the right times for audio-only files)
- **Lyrics overlay**: no VideoWidget anymore — need a QLabel overlay on MpvWidget
- **A/V sync**: both engines seek to same position but may drift independently over time
