#pragma once

#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>

class MprisController;

// org.mpris.MediaPlayer2 (root interface)
class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(bool Fullscreen READ fullscreen WRITE setFullscreen)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(MprisController *parent);

    QString identity() const;
    QString desktopEntry() const;
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    bool fullscreen() const;
    void setFullscreen(bool on);
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();

private:
    MprisController *m_ctrl = nullptr;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(MprisController *parent);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const;
    void setRate(double value);
    double minimumRate() const { return 0.25; }
    double maximumRate() const { return 4.0; }
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double value);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong Offset);
    void SetPosition(const QDBusObjectPath &TrackId, qlonglong Position);
    void OpenUri(const QString &Uri);

signals:
    void Seeked(qlonglong Position);

private:
    MprisController *m_ctrl = nullptr;
};

// Owns the D-Bus object and the state exposed by both adaptors.
class MprisController : public QObject
{
    Q_OBJECT
public:
    explicit MprisController(QObject *parent = nullptr);
    ~MprisController() override;

    bool available() const { return m_available; }

    void setSource(const QString &path, const QString &title,
                   double durationSeconds, const QString &artUrl);
    void setPlaybackStatus(const QString &status);
    void setPositionSeconds(double seconds);
    void setVolumePercent(int percent);
    void setRate(double rate);
    void setRepeatMode(int mode);   // 0 none, 1 playlist, 2 track
    void setShuffle(bool on);
    void setFullscreen(bool on);
    void setCanSeek(bool on);
    void setHasMedia(bool on);
    void emitSeeked(double seconds);

    QString playbackStatus() const { return m_status; }
    QString loopStatus() const;
    double rate() const { return m_rate; }
    bool shuffle() const { return m_shuffle; }
    QVariantMap metadata() const { return m_metadata; }
    double volume() const { return m_volume; }
    qlonglong position() const { return m_positionUs; }
    bool fullscreen() const { return m_fullscreen; }
    bool canSeek() const { return m_canSeek; }
    bool hasMedia() const { return m_hasMedia; }
    MprisPlayerAdaptor *playerAdaptor() const { return m_playerAdaptor; }

signals:
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(double offsetSeconds);
    void setPositionRequested(double seconds);
    void openUriRequested(const QString &uri);
    void raiseRequested();
    void quitRequested();
    void volumeChangeRequested(double volume01);
    void rateChangeRequested(double rate);
    void repeatModeChangeRequested(int mode);
    void shuffleChangeRequested(bool on);
    void fullscreenChangeRequested(bool on);

private:
    void emitPropertiesChanged(const QString &iface, const QVariantMap &changed);

    QDBusConnection m_bus;
    MprisRootAdaptor *m_rootAdaptor = nullptr;
    MprisPlayerAdaptor *m_playerAdaptor = nullptr;
    bool m_available = false;

    QString m_status = QStringLiteral("Stopped");
    QString m_loop = QStringLiteral("None");
    int m_repeatMode = 0;
    double m_rate = 1.0;
    bool m_shuffle = false;
    QVariantMap m_metadata;
    double m_volume = 1.0;
    qlonglong m_positionUs = 0;
    bool m_fullscreen = false;
    bool m_canSeek = false;
    bool m_hasMedia = false;
};
