#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QElapsedTimer>
#include <QImage>
#include <atomic>
#include <vector>
#include <complex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
}

#include "core/types.h"

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool openFile(const QString &filename);
    void play();
    void pause();
    void stop();
    void seek(double position);

    double duration() const { return m_duration; }
    double position() const;
    bool isPlaying() const { return m_playing; }
    float volume() const { return m_volume; }
    void setVolume(float v) { m_volume = std::max(0.0f, std::min(2.0f, v)); }

    QString audioCodecName() const { return m_audioCodecName; }
    QList<AudioTrackInfo> availableAudioTracks() const { return m_audioTracks; }
    int currentAudioTrack() const { return m_audioStream; }
    void selectAudioTrack(int streamIndex);
    int channelMode() const { return m_channelMode; }
    void setChannelMode(int mode) { m_channelMode = mode; }

    QString currentLyricLine() const { return m_currentLyric; }
    const QList<LyricLine> &lyrics() const { return m_lyrics; }
    void loadLyrics(const QString &audioFilePath);

    void closeAll();

    bool hasVideoStreams() const;
    bool hasEmbeddedPoster() const;
    QImage extractEmbeddedPoster() const;
    void triggerSpectrumUpdate();

signals:
    void audioTracksChanged();
    void positionChanged(double position);
    void durationChanged(double duration);
    void stateChanged(bool playing);
    void mediaError(const QString &message);
    void finished();
    void spectrumReady(QVector<double> mags, QVector<double> peaks);
    void lyricChanged(const QString &text);

private:
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);

    bool initSDL();
    bool openAudio();
    double getFramePts(AVFrame *frame, int streamIndex);

    void decodeLoop();
    void clearQueues();
    void captureAudioSamples(const Uint8 *stream, int len);
    void computeSpectrum();

    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;

    int m_audioStream = -1;
    double m_duration = 0.0;
    AVRational m_audioTimeBase;

    // SDL audio
    SDL_AudioDeviceID m_audioDevice = 0;
    AudioSpec m_audioSpec{};
    float m_volume = 1.0f;
    int m_channelMode = 0;

    // Threads
    QThread *m_decodeThread = nullptr;

    QMutex m_formatMutex;
    QMutex m_codecMutex;

    // Audio packets and buffers
    QQueue<AVPacket *> m_audioPackets;
    QMutex m_packetMutex;
    QWaitCondition m_packetCond;

    QQueue<QByteArray> m_audioBuffers;
    QMutex m_audioMutex;
    QWaitCondition m_audioCond;
    int m_audioBufferPos = 0;
    double m_audioSeekPos = 0.0;

    // Timing
    double m_audioClock = 0.0;
    double m_currentPts = 0.0;

    // State
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_seeking{false};
    std::atomic<bool> m_eof{false};
    double m_seekPos = 0.0;
    double m_skipUntilPts = -1.0;
    std::atomic<int64_t> m_audioBytesConsumed{0};

    // Audio info
    QString m_audioCodecName;
    QList<AudioTrackInfo> m_audioTracks;
    bool m_hasVideo = false;

    // Lyrics
    QList<LyricLine> m_lyrics;
    int m_lyricIndex = -1;
    QString m_currentLyric;
    void parseLrcFile(const QString &path);
    void parseLrcContent(const QString &content);

    // Spectrum analyzer
    static constexpr int kFftSize = 2048;
    static constexpr int kSpectrumBars = 32;
    double m_spectrumSamples[kFftSize] = {};
    int m_spectrumWritePos = 0;
    double m_spectrumMag[kSpectrumBars] = {};
    double m_spectrumPeaks[kSpectrumBars] = {};
    QMutex m_spectrumMutex;
};
