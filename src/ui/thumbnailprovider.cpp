#include "ui/thumbnailprovider.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace {

// One-shot decode of the closest keyframe at/after `seconds`. Returns an
// RGB32 QImage scaled to a small thumbnail, or a null image on any failure.
QImage decodeFrame(const QString &path, double seconds)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0 || !fmt)
        return {};

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return {};
    }

    const AVCodec *codec = nullptr;
    const int streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                                                &codec, 0);
    if (streamIndex < 0 || !codec) {
        avformat_close_input(&fmt);
        return {};
    }

    AVStream *stream = fmt->streams[streamIndex];
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx || avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0) {
        if (ctx) avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return {};
    }

    // Seek to the requested time (stream time base).
    const double timeBase = av_q2d(stream->time_base) > 0 ? av_q2d(stream->time_base) : 1.0;
    qint64 target = static_cast<qint64>(seconds / timeBase);
    if (stream->start_time != AV_NOPTS_VALUE)
        target += stream->start_time;
    if (av_seek_frame(fmt, streamIndex, target, AVSEEK_FLAG_BACKWARD) >= 0)
        avcodec_flush_buffers(ctx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    for (int i = 0; i < 200 && result.isNull(); ++i) {
        if (av_read_frame(fmt, packet) < 0)
            break;
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet);
            continue;
        }
        bool gotFrame = false;
        if (avcodec_send_packet(ctx, packet) == 0) {
            while (avcodec_receive_frame(ctx, frame) == 0) {
                const double pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                    ? frame->best_effort_timestamp * timeBase : -1.0;
                gotFrame = true;
                // The first decoded frame after a backwards seek is the keyframe;
                // use it unless we already passed the requested timestamp, in
                // which case this frame is even closer.
                if (pts < 0.0 || pts >= seconds - 0.25)
                    break;
            }
        }
        av_packet_unref(packet);
        if (!gotFrame)
            continue;

        const int srcW = frame->width;
        const int srcH = frame->height;
        if (srcW <= 0 || srcH <= 0)
            break;

        int dstW = 176;
        int dstH = qMax(1, static_cast<int>(srcH * static_cast<double>(dstW) / srcW));
        if (dstH > 110) {
            dstH = 110;
            dstW = qMax(1, static_cast<int>(srcW * static_cast<double>(dstH) / srcH));
        }
        QImage image(dstW, dstH, QImage::Format_RGB32);
        if (image.isNull())
            break;

        SwsContext *sws = sws_getContext(
            srcW, srcH, static_cast<AVPixelFormat>(frame->format),
            dstW, dstH, AV_PIX_FMT_RGB32, SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr);
        if (sws) {
            uint8_t *dstData[4] = {image.bits(), nullptr, nullptr, nullptr};
            int dstLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, srcH, dstData, dstLinesize);
            sws_freeContext(sws);
            result = image;
        }
        break;
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return result;
}

class ThumbJob : public QRunnable
{
public:
    ThumbJob(ThumbnailProvider *provider, const QString &path, double seconds)
        : m_provider(provider), m_path(path), m_seconds(seconds)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        const QImage image = decodeFrame(m_path, m_seconds);
        QMetaObject::invokeMethod(m_provider,
            [provider = m_provider, seconds = m_seconds, image]() {
                provider->onDecoded(seconds, image);
            }, Qt::QueuedConnection);
    }

private:
    ThumbnailProvider *m_provider;
    QString m_path;
    double m_seconds;
};

} // namespace

ThumbnailProvider::ThumbnailProvider(QObject *parent)
    : QObject(parent)
{
}

void ThumbnailProvider::setSource(const QString &localPath)
{
    if (m_path == localPath)
        return;
    m_path = localPath;
    clear();
}

void ThumbnailProvider::clear()
{
    m_cache.clear();
    m_pendingKey = -1;
}

void ThumbnailProvider::request(double seconds)
{
    if (m_path.isEmpty() || seconds < 0.0)
        return;
    const int key = static_cast<int>(seconds / kBucketSeconds);
    if (key == m_pendingKey)
        return;
    if (m_cache.contains(key)) {
        emit ready(key * kBucketSeconds, m_cache.value(key));
        return;
    }
    m_pendingKey = key;
    const double target = key * kBucketSeconds;
    QThreadPool::globalInstance()->start(new ThumbJob(this, m_path, target));
}

void ThumbnailProvider::onDecoded(double seconds, const QImage &image)
{
    const int key = static_cast<int>(seconds / kBucketSeconds);
    if (key == m_pendingKey)
        m_pendingKey = -1;
    if (!image.isNull()) {
        if (m_cache.size() > 80)
            m_cache.clear();
        m_cache.insert(key, image);
    }
    qDebug() << "[thumb] decoded" << seconds << image.size();
    emit ready(seconds, image);
}
