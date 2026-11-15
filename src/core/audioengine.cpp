#include "core/audioengine.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include <iconv.h>

extern "C" {
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

// ----- FFT implementation (radix-2) -----
static void fftRadix2(std::complex<double> *x, int n)
{
    if (n <= 1) return;
    const int half = n / 2;
    std::vector<std::complex<double>> even(half), odd(half);
    for (int i = 0; i < half; ++i) {
        even[i] = x[2 * i];
        odd[i] = x[2 * i + 1];
    }
    fftRadix2(even.data(), half);
    fftRadix2(odd.data(), half);
    for (int k = 0; k < half; ++k) {
        std::complex<double> t = std::polar(1.0, -2.0 * M_PI * k / n) * odd[k];
        x[k] = even[k] + t;
        x[k + half] = even[k] - t;
    }
}

static void computeMusicSpectrum(const double *samples, int n, double *outMag, int numBars)
{
    std::vector<std::complex<double>> f(n);
    for (int i = 0; i < n; ++i) {
        const double hann = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
        f[i] = std::complex<double>(samples[i] * hann, 0.0);
    }
    fftRadix2(f.data(), n);

    const double minFreq = 20.0;
    const double maxFreq = 20000.0;
    const double sampleRate = 48000.0;
    for (int b = 0; b < numBars; ++b) {
        const double lo = minFreq * std::pow(maxFreq / minFreq, double(b) / numBars);
        const double hi = minFreq * std::pow(maxFreq / minFreq, double(b + 1) / numBars);
        int loBin = std::max(1, int(lo / sampleRate * n));
        int hiBin = std::min(n / 2 - 1, int(hi / sampleRate * n));
        if (hiBin < loBin) hiBin = loBin;
        double energy = 0.0;
        for (int k = loBin; k <= hiBin; ++k)
            energy += std::norm(f[k]);
        const int count = hiBin - loBin + 1;
        const double rms = count > 0 ? std::sqrt(energy / count) : 0.0;
        double normalized = std::max(0.0, std::min(1.0, rms / (n * 0.5)));
        outMag[b] = std::sqrt(normalized);
    }
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    closeAll();
}

bool AudioEngine::openFile(const QString &filename)
{
    closeAll();
    m_abort = false;
    m_paused = false;
    m_seekRequested = false;

    const QByteArray path = filename.toUtf8();
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "500000000", 0);
    av_dict_set(&opts, "analyzeduration", "60000000", 0);

    if (avformat_open_input(&m_formatCtx, path.constData(), nullptr, &opts) != 0) {
        av_dict_free(&opts);
        emit mediaError(QStringLiteral("Cannot open file"));
        return false;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit mediaError(QStringLiteral("Cannot find stream info"));
        return false;
    }
    m_duration = static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;

    m_audioTracks.clear();
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        AVStream *st = m_formatCtx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (m_audioStream < 0)
            m_audioStream = static_cast<int>(i);

        AudioTrackInfo info;
        info.streamIndex = static_cast<int>(i);
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
        info.language = lang ? QString::fromUtf8(lang->value) : QString();
        AVDictionaryEntry *title = av_dict_get(st->metadata, "title", nullptr, 0);
        QString name = title ? QString::fromUtf8(title->value) : QString();
        if (name.isEmpty())
            name = info.language.isEmpty()
                       ? QStringLiteral("Track %1").arg(i)
                       : languageName(info.language);
        const char *codecName = avcodec_get_name(st->codecpar->codec_id);
        info.displayName = QStringLiteral("%1: %2, %3")
                               .arg(i)
                               .arg(name, codecName ? QString::fromLatin1(codecName)
                                                    : QStringLiteral("?"));
        m_audioTracks.append(info);
    }

    if (m_audioStream >= 0) {
        AVStream *stream = m_formatCtx->streams[m_audioStream];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            m_audioCodecCtx = avcodec_alloc_context3(codec);
            if (m_audioCodecCtx &&
                avcodec_parameters_to_context(m_audioCodecCtx, stream->codecpar) >= 0) {
                m_audioCodecCtx->pkt_timebase = stream->time_base;
                if (avcodec_open2(m_audioCodecCtx, codec, nullptr) < 0) {
                    avcodec_free_context(&m_audioCodecCtx);
                } else {
                    m_audioCodecName = QString::fromLatin1(
                        avcodec_get_name(stream->codecpar->codec_id));
                    AVChannelLayout outLayout;
                    av_channel_layout_default(&outLayout, 2);
                    const AVChannelLayout *inLayout =
                        stream->codecpar->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
                            ? &stream->codecpar->ch_layout : nullptr;
                    SwrContext *swr = nullptr;
                    if (inLayout &&
                        swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16,
                                            kAnalysisRate, inLayout,
                                            static_cast<AVSampleFormat>(stream->codecpar->format),
                                            stream->codecpar->sample_rate, 0, nullptr) >= 0 &&
                        swr && swr_init(swr) >= 0) {
                        m_swrCtx = swr;
                    } else if (swr) {
                        swr_free(&swr);
                    }
                }
            }
        }
    }

    avformat_seek_file(m_formatCtx, -1, 0, 0, 0, 0);
    return true;
}

void AudioEngine::closeAll()
{
    m_abort = true;
    if (m_decodeThread) {
        m_decodeThread->wait(3000);
        if (m_decodeThread->isRunning())
            m_decodeThread->terminate();
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }

    if (m_swrCtx)
        swr_free(&m_swrCtx);
    if (m_audioCodecCtx)
        avcodec_free_context(&m_audioCodecCtx);
    if (m_formatCtx)
        avformat_close_input(&m_formatCtx);

    m_audioStream = -1;
    m_duration = 0.0;
    m_audioCodecName.clear();
    m_audioTracks.clear();
    m_lyrics.clear();
    m_seekRequested = false;

    {
        QMutexLocker lock(&m_ringMutex);
        m_ring.clear();
    }
    {
        QMutexLocker lock(&m_spectrumMutex);
        m_spectrumWritePos = 0;
        std::fill(m_spectrumSamples, m_spectrumSamples + kFftSize, 0.0);
        std::fill(m_spectrumMag, m_spectrumMag + kSpectrumBars, 0.0);
        std::fill(m_spectrumPeaks, m_spectrumPeaks + kSpectrumBars, 0.0);
    }
}

void AudioEngine::startAnalysis()
{
    if (m_decodeThread || !m_audioCodecCtx || !m_swrCtx || !m_formatCtx)
        return;
    m_abort = false;
    m_decodeThread = QThread::create([this] { decodeLoop(); });
    m_decodeThread->start();
}

void AudioEngine::setAnalysisPaused(bool paused)
{
    m_paused = paused;
}

void AudioEngine::seek(double position)
{
    m_seekPos = std::max(0.0, position);
    m_seekRequested = true;
}

void AudioEngine::appendPcm(const uint8_t *data, int bytes)
{
    if (!data || bytes <= 0)
        return;
    QMutexLocker lock(&m_ringMutex);
    m_ring.append(reinterpret_cast<const char *>(data), bytes);
    if (m_ring.size() > kRingLimit)
        m_ring.remove(0, m_ring.size() - kRingLimit);
}

void AudioEngine::decodeLoop()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        return;
    }

    while (!m_abort.load()) {
        if (m_paused.load()) {
            QThread::msleep(20);
            continue;
        }
        {
            QMutexLocker lock(&m_ringMutex);
            if (m_ring.size() >= kRingLimit) {
                lock.unlock();
                QThread::msleep(10);
                continue;
            }
        }

        if (m_seekRequested.exchange(false)) {
            const int64_t ts = static_cast<int64_t>(m_seekPos * AV_TIME_BASE);
            avformat_seek_file(m_formatCtx, -1, ts / 2, ts, ts * 2, 0);
            avcodec_flush_buffers(m_audioCodecCtx);
            QMutexLocker lock(&m_ringMutex);
            m_ring.clear();
        }

        const int ret = av_read_frame(m_formatCtx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            QThread::msleep(40); // EOF: idle until a seek arrives
            continue;
        }
        if (pkt->stream_index != m_audioStream) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(m_audioCodecCtx, pkt) == 0) {
            while (avcodec_receive_frame(m_audioCodecCtx, frame) == 0) {
                const int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
                if (outSamples > 0 && m_swrCtx) {
                    const int outBytes = av_samples_get_buffer_size(
                        nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 1);
                    if (outBytes > 0) {
                        QByteArray tmp(outBytes, '\0');
                        uint8_t *outPtr = reinterpret_cast<uint8_t *>(tmp.data());
                        const uint8_t *inData[8] = {};
                        const int chCount = std::min(frame->ch_layout.nb_channels, 8);
                        for (int ch = 0; ch < chCount; ++ch)
                            inData[ch] = frame->extended_data[ch];
                        const int converted = swr_convert(
                            m_swrCtx, &outPtr, outSamples, inData, frame->nb_samples);
                        if (converted > 0) {
                            const int bytes = av_samples_get_buffer_size(
                                nullptr, 2, converted, AV_SAMPLE_FMT_S16, 1);
                            appendPcm(reinterpret_cast<const uint8_t *>(tmp.constData()), bytes);
                        }
                    }
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void AudioEngine::triggerSpectrumUpdate()
{
    QByteArray chunk;
    if (!m_paused.load()) {
        QMutexLocker lock(&m_ringMutex);
        // 30ms of S16 stereo at the analysis rate
        const int bytesPerTick = static_cast<int>(kAnalysisRate * 2 * 2 * 0.03);
        const int n = qMin(bytesPerTick, m_ring.size());
        if (n > 0) {
            chunk = m_ring.left(n);
            m_ring.remove(0, n);
        }
    }

    QVector<double> mags(kSpectrumBars), peaks(kSpectrumBars);
    {
        QMutexLocker lock(&m_spectrumMutex);
        bool haveFrame = false;
        if (!chunk.isEmpty()) {
            const int16_t *s = reinterpret_cast<const int16_t *>(chunk.constData());
            const int count = chunk.size() / static_cast<int>(sizeof(int16_t));
            for (int i = 0; i + 1 < count; i += 2) {
                m_spectrumSamples[m_spectrumWritePos] =
                    (static_cast<double>(s[i]) + static_cast<double>(s[i + 1])) / 65536.0;
                m_spectrumWritePos = (m_spectrumWritePos + 1) % kFftSize;
                if (m_spectrumWritePos == 0) {
                    haveFrame = true;
                    break;
                }
            }
        }

        double raw[kSpectrumBars] = {};
        if (haveFrame)
            computeMusicSpectrum(m_spectrumSamples, kFftSize, raw, kSpectrumBars);

        for (int i = 0; i < kSpectrumBars; ++i) {
            const double target = haveFrame ? raw[i] : 0.0;
            const double rate = target > m_spectrumMag[i] ? 0.35 : 0.12;
            m_spectrumMag[i] += (target - m_spectrumMag[i]) * rate;
            if (m_spectrumMag[i] >= m_spectrumPeaks[i])
                m_spectrumPeaks[i] = m_spectrumMag[i] + 0.02;
            else
                m_spectrumPeaks[i] = std::max(0.0, m_spectrumPeaks[i] - 0.006);
            mags[i] = m_spectrumMag[i];
            peaks[i] = m_spectrumPeaks[i];
        }
    }
    emit spectrumReady(mags, peaks);
}

bool AudioEngine::hasVideoStreams() const
{
    if (!m_formatCtx) return false;
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            !(m_formatCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return true;
    }
    return false;
}

bool AudioEngine::hasEmbeddedPoster() const
{
    if (!m_formatCtx) return false;
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (m_formatCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return true;
    }
    return false;
}

QImage AudioEngine::extractEmbeddedPoster() const
{
    if (!m_formatCtx) return {};
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        AVStream *st = m_formatCtx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
            !(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;

        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) continue;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        if (!ctx) continue;
        if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
            avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            continue;
        }
        AVFrame *frame = av_frame_alloc();
        if (!frame) { avcodec_free_context(&ctx); continue; }

        QImage img;
        if (avcodec_send_packet(ctx, &st->attached_pic) >= 0 &&
            avcodec_receive_frame(ctx, frame) >= 0) {
            const int w = frame->width;
            const int h = frame->height;
            if (w > 0 && h > 0) {
                SwsContext *sws = sws_getContext(w, h, static_cast<AVPixelFormat>(frame->format),
                                                 w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                                 nullptr, nullptr, nullptr);
                if (sws) {
                    QByteArray rgb(w * h * 3, '\0');
                    uint8_t *dst[1] = {reinterpret_cast<uint8_t *>(rgb.data())};
                    int dstStride[1] = {w * 3};
                    sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
                    sws_freeContext(sws);
                    img = QImage(reinterpret_cast<const uchar *>(rgb.constData()),
                                 w, h, w * 3, QImage::Format_RGB888).copy();
                }
            }
        }
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return img;
    }
    return {};
}

void AudioEngine::loadLyrics(const QString &audioFilePath)
{
    m_lyrics.clear();

    if (m_formatCtx) {
        AVDictionaryEntry *tag = av_dict_get(m_formatCtx->metadata, "lyrics", nullptr, 0);
        if (!tag) tag = av_dict_get(m_formatCtx->metadata, "LYRICS", nullptr, 0);
        if (!tag) tag = av_dict_get(m_formatCtx->metadata, "\xa9lyr", nullptr, 0);
        if (tag && tag->value) {
            parseLrcContent(QString::fromUtf8(tag->value));
            if (!m_lyrics.isEmpty()) return;
        }
    }

    const QFileInfo fi(audioFilePath);
    const QString lrcPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".lrc";
    if (QFile::exists(lrcPath))
        parseLrcFile(lrcPath);
}

static bool tryDecodeIconv(QByteArray &result, const QByteArray &input, const char *fromEnc)
{
    iconv_t cd = iconv_open("UTF-8", fromEnc);
    if (cd == reinterpret_cast<iconv_t>(-1)) return false;

    size_t inLen = static_cast<size_t>(input.size());
    size_t outLen = inLen * 4 + 64;
    QByteArray buf(static_cast<int>(outLen), '\0');
    char *inPtr = const_cast<char *>(input.constData());
    char *outPtr = buf.data();

    const size_t ret = iconv(cd, &inPtr, &inLen, &outPtr, &outLen);
    iconv_close(cd);
    if (ret == static_cast<size_t>(-1)) return false;

    result = buf.left(buf.size() - static_cast<int>(outLen));
    return true;
}

void AudioEngine::parseLrcFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = file.readAll();
    file.close();

    if (raw.size() >= 3 && uchar(raw[0]) == 0xEF &&
        uchar(raw[1]) == 0xBB && uchar(raw[2]) == 0xBF) {
        parseLrcContent(QString::fromUtf8(raw.constData() + 3, raw.size() - 3));
        return;
    }

    {
        QStringDecoder dec(QStringConverter::Utf8);
        const QString text = dec(raw);
        if (!dec.hasError()) {
            parseLrcContent(text);
            return;
        }
    }

    const char *encodings[] = {"GB18030", "GBK", "Big5", "Shift-JIS", "EUC-KR"};
    for (const char *enc : encodings) {
        QByteArray converted;
        if (tryDecodeIconv(converted, raw, enc)) {
            parseLrcContent(QString::fromUtf8(converted));
            return;
        }
    }

    QStringDecoder decSys(QStringConverter::System);
    parseLrcContent(decSys(raw));
}

void AudioEngine::parseLrcContent(const QString &content)
{
    static const QRegularExpression re(R"(\[(\d+):(\d+(?:\.\d+)?)\](.*))");
    for (const QString &line : content.split('\n')) {
        const QRegularExpressionMatch m = re.match(line.trimmed());
        if (m.hasMatch()) {
            LyricLine l;
            l.timestamp = m.captured(1).toDouble() * 60.0 + m.captured(2).toDouble();
            l.text = m.captured(3).trimmed();
            if (!l.text.isEmpty())
                m_lyrics.append(l);
        }
    }
    std::sort(m_lyrics.begin(), m_lyrics.end(),
              [](const LyricLine &a, const LyricLine &b) { return a.timestamp < b.timestamp; });
}
