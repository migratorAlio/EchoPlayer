#include "core/audioengine.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <iconv.h>
#include <cmath>
#include <complex>

// ----- FFT implementation (radix-2) -----
static void fftRadix2(std::complex<double> *x, int n)
{
    if (n <= 1) return;
    int half = n / 2;
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

// ----- Music spectrum helper -----
static void computeMusicSpectrum(const double *samples, int n,
                                 double *outMag, double *outPeaks, int numBars)
{
    std::vector<std::complex<double>> f(n);
    // Apply Hann window and convert to complex
    for (int i = 0; i < n; ++i) {
        double hann = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
        f[i] = std::complex<double>(samples[i] * hann, 0.0);
    }
    fftRadix2(f.data(), n);

    // Energy in log-spaced frequency bins (20Hz - 20kHz)
    const double minFreq = 20.0;
    const double maxFreq = 20000.0;
    const double sampleRate = 48000.0;
    for (int b = 0; b < numBars; ++b) {
        double lo = minFreq * std::pow(maxFreq / minFreq, double(b) / numBars);
        double hi = minFreq * std::pow(maxFreq / minFreq, double(b + 1) / numBars);
        int loBin = std::max(1, int(lo / sampleRate * n));
        int hiBin = std::min(n / 2 - 1, int(hi / sampleRate * n));
        if (hiBin < loBin) hiBin = loBin;
        double energy = 0.0;
        for (int k = loBin; k <= hiBin; ++k) {
            double mag = std::abs(f[k]);
            energy += mag * mag;
        }
        int count = hiBin - loBin + 1;
        double rms = count > 0 ? std::sqrt(energy / count) : 0.0;
        // Normalize by max possible RMS per bin (Hann window gain ≈ n/2)
        double normalized = std::max(0.0, std::min(1.0, rms / (n * 0.5)));
        // Apply power curve for visual clarity
        normalized = std::sqrt(normalized);
        outMag[b] = normalized;
    }
}


AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    stop();
    closeAll();
}

bool AudioEngine::openFile(const QString &filename)
{
    stop();
    closeAll();

    QByteArray path = filename.toUtf8();

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "500000000", 0);
    av_dict_set(&opts, "analyzeduration", "60000000", 0);

    if (avformat_open_input(&m_formatCtx, path.constData(), nullptr, &opts) != 0) {
        av_dict_free(&opts);
        emit mediaError("Cannot open file");
        return false;
    }
    av_dict_free(&opts);

    m_formatCtx->probesize = 500 * 1024 * 1024;
    m_formatCtx->max_analyze_duration = 60 * AV_TIME_BASE;

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit mediaError("Cannot find stream info");
        return false;
    }

    m_duration = static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;

    m_audioTracks.clear();
    m_hasVideo = false;
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        auto type = m_formatCtx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO)
            m_hasVideo = true;
        if (type == AVMEDIA_TYPE_AUDIO) {
            if (m_audioStream < 0)
                m_audioStream = i;
            AudioTrackInfo info;
            info.streamIndex = i;
            AVDictionaryEntry *lang = av_dict_get(m_formatCtx->streams[i]->metadata, "language", nullptr, 0);
            QString code = lang ? QString::fromUtf8(lang->value) : QString();
            info.language = code;
            AVDictionaryEntry *title = av_dict_get(m_formatCtx->streams[i]->metadata, "title", nullptr, 0);
            QString name = title ? QString::fromUtf8(title->value) : QString();
            if (name.isEmpty())
                name = code.isEmpty() ? QString("Track %1").arg(i) : languageName(code);
            const char *codecName = avcodec_get_name(m_formatCtx->streams[i]->codecpar->codec_id);
            info.displayName = QString("%1: %2, %3").arg(i).arg(name, codecName ? QString::fromLatin1(codecName) : "?");
            m_audioTracks.append(info);
        }
    }

    // Seek to beginning in case avformat_find_stream_info consumed all packets
    if (m_formatCtx)
        avformat_seek_file(m_formatCtx, -1, 0, 0, 0, 0);

    return openAudio();
}

double AudioEngine::position() const
{
    if (m_audioSpec.sampleRate > 0 && m_audioSpec.channels > 0) {
        double bytesPerSec = static_cast<double>(m_audioSpec.sampleRate) *
                             m_audioSpec.channels * sizeof(int16_t);
        double pos = m_audioBytesConsumed.load() / bytesPerSec;
        if (pos > m_currentPts)
            const_cast<AudioEngine *>(this)->m_currentPts = pos;
    }
    return m_currentPts;
}

void AudioEngine::play()
{
    if (m_playing || !m_audioCodecCtx) return;
    m_playing = true;

    if (!m_decodeThread) {
        m_abort = false;
        m_decodeThread = QThread::create([this] {
            // Seek to beginning — avformat_find_stream_info may have consumed packets
            if (m_formatCtx) {
                QMutexLocker lock(&m_formatMutex);
                avformat_seek_file(m_formatCtx, -1, 0, 0, 0, 0);
            }
            decodeLoop();
        });
        m_decodeThread->start();
    }

    if (m_audioDevice)
        SDL_PauseAudioDevice(m_audioDevice, 0);

    emit stateChanged(true);
}

void AudioEngine::pause()
{
    if (!m_playing) return;

    if (m_audioDevice)
        SDL_PauseAudioDevice(m_audioDevice, 1);

    m_playing = false;
    emit stateChanged(false);
}

void AudioEngine::stop()
{
    if (m_abort && !m_playing) return;

    m_playing = false;
    m_abort = true;

    if (m_audioDevice)
        SDL_PauseAudioDevice(m_audioDevice, 1);

    m_packetCond.wakeAll();
    m_audioCond.wakeAll();

    if (m_decodeThread && m_decodeThread->isRunning()) {
        if (!m_decodeThread->wait(3000)) {
            m_decodeThread->terminate();
            m_decodeThread->wait();
        }
    }
    delete m_decodeThread;
    m_decodeThread = nullptr;

    clearQueues();
    m_currentPts = 0.0;
    m_audioClock = 0.0;
    m_audioBytesConsumed.store(0);
    emit stateChanged(false);
    emit positionChanged(0);
}

void AudioEngine::seek(double position)
{
    if (!m_formatCtx) return;
    m_seeking = true;
    m_eof = false;
    m_seekPos = std::max(0.0, std::min(position, m_duration));

    {
        QMutexLocker lock(&m_formatMutex);
        int64_t ts = static_cast<int64_t>(m_seekPos * AV_TIME_BASE);
        avformat_seek_file(m_formatCtx, -1, INT64_MIN, ts, INT64_MAX, AVSEEK_FLAG_BACKWARD);
    }

    {
        QMutexLocker lock(&m_codecMutex);
        if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
    }

    clearQueues();
    {
        QMutexLocker lock(&m_audioMutex);
        if (m_audioSpec.sampleRate > 0 && m_audioSpec.channels > 0) {
            double bytesPerSec = static_cast<double>(m_audioSpec.sampleRate) *
                                 m_audioSpec.channels * sizeof(int16_t);
            m_audioBytesConsumed.store(static_cast<int64_t>(m_seekPos * bytesPerSec));
        } else {
            m_audioBytesConsumed.store(0);
        }
        m_audioBufferPos = 0;
        m_audioClock = m_seekPos;
    }
    m_audioSeekPos = m_seekPos;
    m_skipUntilPts = m_seekPos;

    m_seeking = false;
    m_currentPts = m_seekPos;
    emit positionChanged(m_seekPos);
}

void AudioEngine::selectAudioTrack(int streamIndex)
{
    if (!m_formatCtx) return;

    bool wasPlaying = m_playing;
    double pos = m_currentPts;
    QByteArray savedUrl = m_formatCtx->url;

    stop();
    closeAll();

    // Reopen file
    if (!openFile(savedUrl)) return;

    // Switch to the selected stream
    if (streamIndex >= 0 && streamIndex < static_cast<int>(m_formatCtx->nb_streams)) {
        if (m_formatCtx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStream = streamIndex;
            // Re-init audio with the new stream
            if (!openAudio()) return;
        }
    }

    if (wasPlaying) {
        play();
        if (pos > 0) seek(pos);
    }
    emit audioTracksChanged();
}

void AudioEngine::closeAll()
{
    m_playing = false;
    m_abort = true;
    m_duration = 0.0;
    m_currentPts = 0;
    m_audioStream = -1;
    m_audioClock = 0.0;
    m_audioBytesConsumed.store(0);
    m_eof = false;
    m_audioCodecName.clear();
    m_audioTracks.clear();
    m_lyrics.clear();
    m_lyricIndex = -1;
    m_currentLyric.clear();
    m_hasVideo = false;

    {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffers.clear();
        m_audioBufferPos = 0;
        m_audioSeekPos = 0.0;
    }

    {
        QMutexLocker lock(&m_packetMutex);
        while (!m_audioPackets.isEmpty()) {
            AVPacket *pkt = m_audioPackets.dequeue();
            av_packet_free(&pkt);
        }
    }

    if (m_audioDevice) {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }

    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); m_audioCodecCtx = nullptr; }
    if (m_formatCtx) { avformat_close_input(&m_formatCtx); m_formatCtx = nullptr; }
}

bool AudioEngine::hasVideoStreams() const
{
    if (!m_formatCtx) return false;
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            !(m_formatCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            return true;
        }
    }
    return false;
}

bool AudioEngine::hasEmbeddedPoster() const
{
    if (!m_formatCtx) return false;
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (m_formatCtx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            return true;
        }
    }
    return false;
}

QImage AudioEngine::extractEmbeddedPoster() const
{
    if (!m_formatCtx) return {};
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        auto *st = m_formatCtx->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            if (!codec) continue;
            AVCodecContext *ctx = avcodec_alloc_context3(codec);
            if (!ctx) continue;
            if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
                avcodec_free_context(&ctx);
                continue;
            }
            if (avcodec_open2(ctx, codec, nullptr) < 0) {
                avcodec_free_context(&ctx);
                continue;
            }
            AVFrame *frame = av_frame_alloc();
            if (!frame) { avcodec_free_context(&ctx); continue; }
            // Attached picture has the packet data in the stream's side data
            int ret = avcodec_send_packet(ctx, &st->attached_pic);
            if (ret < 0) {
                av_frame_free(&frame);
                avcodec_free_context(&ctx);
                continue;
            }
            ret = avcodec_receive_frame(ctx, frame);
            QImage img;
            if (ret >= 0) {
                // Convert to RGB
                int w = frame->width;
                int h = frame->height;
                if (w > 0 && h > 0) {
                    SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)frame->format,
                                                     w, h, AV_PIX_FMT_RGB24,
                                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (sws) {
                        uint8_t *rgbData = new uint8_t[w * h * 3];
                        uint8_t *dst[1] = {rgbData};
                        int dstStride[1] = {w * 3};
                        sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
                        sws_freeContext(sws);
                        img = QImage(rgbData, w, h, w * 3, QImage::Format_RGB888).copy();
                        delete[] rgbData;
                    }
                }
            }
            av_frame_free(&frame);
            avcodec_free_context(&ctx);
            return img;
        }
    }
    return {};
}

// ----- Audio init -----

bool AudioEngine::initSDL()
{
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            emit mediaError(QString("SDL init failed: %1").arg(SDL_GetError()));
            return false;
        }
    }
    return true;
}

bool AudioEngine::openAudio()
{
    if (m_audioStream < 0) return false;

    AVStream *stream = m_formatCtx->streams[m_audioStream];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    m_audioCodecCtx = avcodec_alloc_context3(codec);
    if (!m_audioCodecCtx) return false;

    if (avcodec_parameters_to_context(m_audioCodecCtx, stream->codecpar) < 0)
        return false;

    m_audioCodecCtx->pkt_timebase = stream->time_base;

    if (avcodec_open2(m_audioCodecCtx, codec, nullptr) < 0)
        return false;

    m_audioTimeBase = stream->time_base;
    m_audioCodecName = QString::fromLatin1(avcodec_get_name(stream->codecpar->codec_id));

    // Init SDL
    if (!initSDL()) return false;

    SDL_AudioSpec want, obtained;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = sdlAudioCallback;
    want.userdata = this;

    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!m_audioDevice) {
        emit mediaError(QString("SDL audio device open failed: %1").arg(SDL_GetError()));
        return false;
    }

    m_audioSpec.sampleRate = obtained.freq;
    m_audioSpec.channels = obtained.channels;
    m_audioSpec.format = obtained.format;

    // Init resampler
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, m_audioSpec.channels);
    SwrContext *swr = nullptr;
    if (swr_alloc_set_opts2(&swr,
        &outLayout, AV_SAMPLE_FMT_S16, m_audioSpec.sampleRate,
        stream->codecpar->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
            ? &stream->codecpar->ch_layout
            : nullptr,
        static_cast<AVSampleFormat>(stream->codecpar->format),
        stream->codecpar->sample_rate,
        0, nullptr) < 0 || !swr)
    {
        emit mediaError("Audio resampler init failed");
        return false;
    }
    m_swrCtx = swr;
    if (swr_init(m_swrCtx) < 0) {
        emit mediaError("Audio resampler init failed");
        return false;
    }

    return true;
}

// ----- Decode loop (audio only) -----

void AudioEngine::decodeLoop()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    m_eof = false;

    while (!m_abort) {
        // 1. Read one packet
        if (!m_eof) {
            bool readError = false;
            {
                QMutexLocker lock(&m_formatMutex);
                if (m_seeking) continue;
                int ret = av_read_frame(m_formatCtx, pkt);
                if (ret == AVERROR_EOF) {
                    m_eof = true;
                } else if (ret < 0) {
                    readError = true;
                }
            }
            if (!readError && !m_eof) {
                if (pkt->stream_index == m_audioStream) {
                    QMutexLocker lock(&m_packetMutex);
                    AVPacket *dup = av_packet_alloc();
                    av_packet_ref(dup, pkt);
                    m_audioPackets.enqueue(dup);
                    m_packetCond.wakeOne();
                }
                av_packet_unref(pkt);
            } else if (readError) {
                QThread::msleep(10);
            }
        }

        // 2. Process all queued audio packets
        if (m_audioCodecCtx && m_swrCtx) {
            while (true) {
                AVPacket *apkt = nullptr;
                {
                    QMutexLocker lock(&m_packetMutex);
                    if (!m_audioPackets.isEmpty())
                        apkt = m_audioPackets.dequeue();
                }
                if (!apkt) break;

                QMutexLocker lock(&m_codecMutex);
                if (!m_seeking && avcodec_send_packet(m_audioCodecCtx, apkt) == 0) {
                    while (true) {
                        int r = avcodec_receive_frame(m_audioCodecCtx, frame);
                        if (r < 0) break;

                        if (m_skipUntilPts >= 0) {
                            double aPts = getFramePts(frame, m_audioStream);
                            if (aPts < m_skipUntilPts) {
                                av_frame_unref(frame);
                                continue;
                            }
                            m_skipUntilPts = -1.0;
                        }

                        uint8_t *out[1] = {nullptr};
                        int outSamples = av_rescale_rnd(
                            swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate) + frame->nb_samples,
                            m_audioSpec.sampleRate, m_audioCodecCtx->sample_rate, AV_ROUND_UP);

                        if (av_samples_alloc(out, nullptr, m_audioSpec.channels, outSamples,
                                             AV_SAMPLE_FMT_S16, 0) < 0)
                            continue;

                        int converted = swr_convert(m_swrCtx, out, outSamples,
                                                    (const uint8_t **)frame->data, frame->nb_samples);

                        int bufSize = av_samples_get_buffer_size(nullptr, m_audioSpec.channels,
                                                                 converted, AV_SAMPLE_FMT_S16, 1);
                        if (bufSize > 0) {
                            QByteArray audioData(reinterpret_cast<const char *>(out[0]), bufSize);
                            {
                                QMutexLocker lock2(&m_audioMutex);
                                m_audioBuffers.enqueue(audioData);
                                m_audioCond.wakeOne();
                            }
                        }
                        av_freep(&out[0]);
                    }
                }
                av_packet_free(&apkt);
            }
        }

        // 3. EOF check
        if (m_eof) {
            // Seek may have reset m_eof — re-check
            if (!m_eof) continue;
            bool packetDone = false;
            {
                QMutexLocker lock(&m_packetMutex);
                if (m_audioPackets.isEmpty())
                    packetDone = true;
            }
            if (packetDone && !m_seeking) {
                // Don't emit finished() until audio buffers are actually drained
                bool bufferEmpty = false;
                {
                    QMutexLocker lock(&m_audioMutex);
                    bufferEmpty = m_audioBuffers.isEmpty();
                }
                if (!bufferEmpty) {
                    // Still playing, check again next iteration
                    QThread::msleep(50);
                    continue;
                }
                emit finished();
                // Wait for seek or abort — don't break, so seek() can restart packet production
                while (!m_abort && m_eof)
                    QThread::msleep(50);
                if (m_abort) break;
                continue;
            }
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);

    if (!m_abort) {
        if (m_audioDevice) {
            while (true) {
                {
                    QMutexLocker lock(&m_audioMutex);
                    if (m_audioBuffers.isEmpty()) break;
                }
                QThread::msleep(100);
            }
        }
        emit finished();
    }
}

void AudioEngine::clearQueues()
{
    {
        QMutexLocker lock(&m_packetMutex);
        while (!m_audioPackets.isEmpty()) {
            AVPacket *pkt = m_audioPackets.dequeue();
            av_packet_free(&pkt);
        }
    }
    {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffers.clear();
        m_audioBufferPos = 0;
    }
}

// ----- SDL audio callback -----

void AudioEngine::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    auto *self = static_cast<AudioEngine *>(userdata);
    SDL_memset(stream, 0, len);

    QMutexLocker lock(&self->m_audioMutex);

    int total = len;
    int written = 0;

    while (total > 0 && !self->m_audioBuffers.isEmpty()) {
        QByteArray &buf = self->m_audioBuffers.head();
        int avail = buf.size() - self->m_audioBufferPos;
        int chunk = qMin(total, avail);

        // Apply volume and channel mixing
        const int16_t *src = reinterpret_cast<const int16_t *>(buf.constData() + self->m_audioBufferPos);
        int16_t *dst = reinterpret_cast<int16_t *>(stream + written);
        int samples = chunk / 2; // 2 bytes per sample

        if (self->m_volume < 0.001f) {
            // Muted: leave dst as zero
        } else if (self->m_volume < 1.0f || self->m_channelMode != 0) {
            for (int i = 0; i < samples; ++i) {
                int val;
                if (self->m_channelMode == 1) {
                    // Left only: both channels = left
                    val = src[i & ~1]; // even index = left
                } else if (self->m_channelMode == 2) {
                    // Right only: both channels = right
                    val = src[i | 1]; // odd index = right
                } else {
                    val = src[i];
                }
                val = qBound(-32768, static_cast<int>(val * self->m_volume), 32767);
                dst[i] = static_cast<int16_t>(val);
            }
        } else {
            memcpy(dst, src, chunk);
        }

        written += chunk;
        total -= chunk;
        self->m_audioBufferPos += chunk;
        self->m_audioBytesConsumed.fetch_add(chunk);

        if (self->m_audioBufferPos >= buf.size()) {
            self->m_audioBufferPos = 0;
            self->m_audioBuffers.dequeue();
        }
    }

    // Capture for spectrum
    self->captureAudioSamples(stream, len);
}

void AudioEngine::captureAudioSamples(const Uint8 *stream, int len)
{
    QMutexLocker lock(&m_spectrumMutex);
    const int16_t *samples = reinterpret_cast<const int16_t *>(stream);
    int count = len / sizeof(int16_t);
    for (int i = 0; i < count; i += 2) { // stereo -> mono
        m_spectrumSamples[m_spectrumWritePos] = samples[i] / 32768.0;
        m_spectrumWritePos = (m_spectrumWritePos + 1) % kFftSize;
        if (m_spectrumWritePos == 0)
            break; // collected one full buffer
    }
}

void AudioEngine::computeSpectrum()
{
    QMutexLocker lock(&m_spectrumMutex);
    if (m_spectrumWritePos > 0)
        return;

    double mag[kSpectrumBars] = {};
    computeMusicSpectrum(m_spectrumSamples, kFftSize, mag, m_spectrumPeaks, kSpectrumBars);

    // Apply attack/decay smoothing
    constexpr double attack = 0.3;
    constexpr double decay = 0.1;
    for (int i = 0; i < kSpectrumBars; ++i) {
        if (mag[i] > m_spectrumMag[i])
            m_spectrumMag[i] += (mag[i] - m_spectrumMag[i]) * attack;
        else
            m_spectrumMag[i] += (mag[i] - m_spectrumMag[i]) * decay;

        // Peak tracking
        if (mag[i] >= m_spectrumPeaks[i])
            m_spectrumPeaks[i] = mag[i] + 0.02;
        else
            m_spectrumPeaks[i] -= 0.005;
        m_spectrumPeaks[i] = std::max(0.0, m_spectrumPeaks[i]);
    }
}

void AudioEngine::triggerSpectrumUpdate()
{
    // Update audio position from bytes consumed
    bool posChanged = false;
    if (m_audioSpec.sampleRate > 0 && m_audioSpec.channels > 0) {
        double bytesPerSec = static_cast<double>(m_audioSpec.sampleRate) *
                             m_audioSpec.channels * sizeof(int16_t);
        double newPos = m_audioBytesConsumed.load() / bytesPerSec;
        if (newPos > m_currentPts) {
            m_currentPts = newPos;
            posChanged = true;
        }
    }
    if (posChanged)
        emit positionChanged(m_currentPts);

    computeSpectrum();
    QVector<double> mags(kSpectrumBars), peaks(kSpectrumBars);
    {
        QMutexLocker lock(&m_spectrumMutex);
        for (int i = 0; i < kSpectrumBars; ++i) {
            mags[i] = m_spectrumMag[i];
            peaks[i] = m_spectrumPeaks[i];
        }
    }
    emit spectrumReady(mags, peaks);
}

double AudioEngine::getFramePts(AVFrame *frame, int streamIndex)
{
    if (frame->pts != AV_NOPTS_VALUE) {
        AVRational tb = m_formatCtx->streams[streamIndex]->time_base;
        return av_q2d(tb) * frame->pts;
    }
    return 0.0;
}

// ----- Lyrics -----

void AudioEngine::loadLyrics(const QString &audioFilePath)
{
    m_lyrics.clear();
    m_lyricIndex = -1;
    m_currentLyric.clear();

    // Try embedded lyrics from FFmpeg metadata
    if (m_formatCtx) {
        AVDictionaryEntry *tag = av_dict_get(m_formatCtx->metadata, "lyrics", nullptr, 0);
        if (!tag) tag = av_dict_get(m_formatCtx->metadata, "LYRICS", nullptr, 0);
        if (!tag) tag = av_dict_get(m_formatCtx->metadata, "\xa9lyr", nullptr, 0);
        if (tag && tag->value) {
            parseLrcContent(QString::fromUtf8(tag->value));
            if (!m_lyrics.isEmpty()) return;
        }
    }

    // Try external .lrc file
    QFileInfo fi(audioFilePath);
    QString lrcPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".lrc";
    if (QFile::exists(lrcPath)) {
        parseLrcFile(lrcPath);
    }
}

static bool tryDecodeIconv(QByteArray &result, const QByteArray &input, const char *fromEnc)
{
    iconv_t cd = iconv_open("UTF-8", fromEnc);
    if (cd == (iconv_t)-1) return false;

    size_t inLen = static_cast<size_t>(input.size());
    size_t outLen = inLen * 4 + 64;
    QByteArray buf(static_cast<int>(outLen), '\0');
    char *inPtr = const_cast<char *>(input.constData());
    char *outPtr = buf.data();

    size_t ret = iconv(cd, &inPtr, &inLen, &outPtr, &outLen);
    iconv_close(cd);

    if (ret == (size_t)-1) return false;

    result = buf.left(buf.size() - static_cast<int>(outLen));
    return true;
}

void AudioEngine::parseLrcFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray raw = file.readAll();
    file.close();

    QString text;

    // Check BOM
    if (raw.size() >= 3 && (uchar(raw[0]) == 0xEF && uchar(raw[1]) == 0xBB && uchar(raw[2]) == 0xBF)) {
        text = QString::fromUtf8(raw.constData() + 3, raw.size() - 3);
        parseLrcContent(text);
        return;
    }

    // Try UTF-8
    {
        QStringDecoder dec(QStringConverter::Utf8);
        text = dec(raw);
        if (!dec.hasError()) {
            parseLrcContent(text);
            return;
        }
    }

    // Try common non-UTF-8 encodings via iconv
    const char *encodings[] = { "GB18030", "GBK", "Big5", "Shift-JIS", "EUC-KR" };
    for (const char *enc : encodings) {
        QByteArray converted;
        if (tryDecodeIconv(converted, raw, enc)) {
            text = QString::fromUtf8(converted);
            parseLrcContent(text);
            return;
        }
    }

    // Final fallback: System locale
    QStringDecoder decSys(QStringConverter::System);
    parseLrcContent(decSys(raw));
}

void AudioEngine::parseLrcContent(const QString &content)
{
    static QRegularExpression re(R"(\[(\d+):(\d+(?:\.\d+)?)\](.*))");
    for (const auto &line : content.split('\n')) {
        QRegularExpressionMatch m = re.match(line.trimmed());
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
