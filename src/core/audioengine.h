#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QImage>
#include <QString>
#include <QList>
#include <QVector>
#include <QByteArray>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "core/types.h"

// Analysis-only companion to mpv. Playback and audio output belong to mpv;
// this class reads the same file to provide metadata, cover art, LRC lyrics
// and a real FFT spectrum (decoded to PCM but never sent to a sound device).
class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool openFile(const QString &filename);
    void closeAll();
    void startAnalysis();
    void setAnalysisPaused(bool paused);
    void seek(double position);
    void triggerSpectrumUpdate();

    double duration() const { return m_duration; }
    QString audioCodecName() const { return m_audioCodecName; }
    QList<AudioTrackInfo> availableAudioTracks() const { return m_audioTracks; }
    bool hasVideoStreams() const;
    bool hasEmbeddedPoster() const;
    QImage extractEmbeddedPoster() const;

    void loadLyrics(const QString &audioFilePath);
    const QList<LyricLine> &lyrics() const { return m_lyrics; }

signals:
    void spectrumReady(QVector<double> mags, QVector<double> peaks);
    void mediaError(const QString &message);

private:
    void decodeLoop();
    void appendPcm(const uint8_t *data, int bytes);
    void parseLrcFile(const QString &path);
    void parseLrcContent(const QString &content);

    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStream = -1;
    double m_duration = 0.0;
    static constexpr int kAnalysisRate = 48000;
    QString m_audioCodecName;
    QList<AudioTrackInfo> m_audioTracks;

    QThread *m_decodeThread = nullptr;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_seekRequested{false};
    double m_seekPos = 0.0;
    QMutex m_formatMutex;

    QMutex m_ringMutex;
    QByteArray m_ring;
    static constexpr int kRingLimit = 262144;

    static constexpr int kFftSize = 2048;
    static constexpr int kSpectrumBars = 32;
    double m_spectrumSamples[kFftSize] = {};
    int m_spectrumWritePos = 0;
    double m_spectrumMag[kSpectrumBars] = {};
    double m_spectrumPeaks[kSpectrumBars] = {};
    QMutex m_spectrumMutex;

    QList<LyricLine> m_lyrics;
};
