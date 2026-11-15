#pragma once

#include <QObject>
#include <QImage>
#include <QHash>
#include <QString>

// Decodes video frames in a background thread for the seek-bar hover preview.
// Frames are quantized to 5-second buckets and cached, so dragging the mouse
// back and forth does not re-decode. Only local files are supported; stream
// callers should simply not create requests.
class ThumbnailProvider : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);

    void setSource(const QString &localPath);
    void request(double seconds);
    void clear();

    static constexpr int kBucketSeconds = 5;

public slots:
    // Internal: called on the provider's thread when a decode job completes.
    void onDecoded(double seconds, const QImage &image);

signals:
    void ready(double seconds, const QImage &image);

private:
    QString m_path;
    QHash<int, QImage> m_cache;
    int m_pendingKey = -1;
};
