#include "core/mpriscontroller.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace {
constexpr const char *kService = "org.mpris.MediaPlayer2.EchoPlayer";
constexpr const char *kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kRootIface = "org.mpris.MediaPlayer2";
constexpr const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
}

// ---------------------------------------------------------------- root adaptor

MprisRootAdaptor::MprisRootAdaptor(MprisController *parent)
    : QDBusAbstractAdaptor(parent), m_ctrl(parent)
{
}

QString MprisRootAdaptor::identity() const
{
    return QStringLiteral("EchoPlayer");
}

QString MprisRootAdaptor::desktopEntry() const
{
    return QStringLiteral("org.echoplayer.EchoPlayer");
}

bool MprisRootAdaptor::fullscreen() const
{
    return m_ctrl->fullscreen();
}

void MprisRootAdaptor::setFullscreen(bool on)
{
    emit m_ctrl->fullscreenChangeRequested(on);
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https"),
            QStringLiteral("rtsp"), QStringLiteral("rtmp"), QStringLiteral("srt"),
            QStringLiteral("udp"), QStringLiteral("ftp")};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {QStringLiteral("video/*"), QStringLiteral("audio/*")};
}

void MprisRootAdaptor::Raise()
{
    emit m_ctrl->raiseRequested();
}

void MprisRootAdaptor::Quit()
{
    emit m_ctrl->quitRequested();
}

// -------------------------------------------------------------- player adaptor

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisController *parent)
    : QDBusAbstractAdaptor(parent), m_ctrl(parent)
{
}

QString MprisPlayerAdaptor::playbackStatus() const { return m_ctrl->playbackStatus(); }
QString MprisPlayerAdaptor::loopStatus() const { return m_ctrl->loopStatus(); }

void MprisPlayerAdaptor::setLoopStatus(const QString &status)
{
    int mode = 0;
    if (status == QLatin1String("Playlist")) mode = 1;
    else if (status == QLatin1String("Track")) mode = 2;
    emit m_ctrl->repeatModeChangeRequested(mode);
}

double MprisPlayerAdaptor::rate() const { return m_ctrl->rate(); }

void MprisPlayerAdaptor::setRate(double value)
{
    emit m_ctrl->rateChangeRequested(value);
}

bool MprisPlayerAdaptor::shuffle() const { return m_ctrl->shuffle(); }

void MprisPlayerAdaptor::setShuffle(bool on)
{
    emit m_ctrl->shuffleChangeRequested(on);
}

QVariantMap MprisPlayerAdaptor::metadata() const { return m_ctrl->metadata(); }
double MprisPlayerAdaptor::volume() const { return m_ctrl->volume(); }

void MprisPlayerAdaptor::setVolume(double value)
{
    emit m_ctrl->volumeChangeRequested(value);
}

qlonglong MprisPlayerAdaptor::position() const { return m_ctrl->position(); }
bool MprisPlayerAdaptor::canGoNext() const { return m_ctrl->hasMedia(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_ctrl->hasMedia(); }
bool MprisPlayerAdaptor::canPlay() const { return m_ctrl->hasMedia(); }
bool MprisPlayerAdaptor::canPause() const { return m_ctrl->hasMedia(); }
bool MprisPlayerAdaptor::canSeek() const { return m_ctrl->canSeek() && m_ctrl->hasMedia(); }

void MprisPlayerAdaptor::Next() { emit m_ctrl->nextRequested(); }
void MprisPlayerAdaptor::Previous() { emit m_ctrl->previousRequested(); }
void MprisPlayerAdaptor::Pause() { emit m_ctrl->pauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { emit m_ctrl->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit m_ctrl->stopRequested(); }
void MprisPlayerAdaptor::Play() { emit m_ctrl->playRequested(); }

void MprisPlayerAdaptor::Seek(qlonglong Offset)
{
    emit m_ctrl->seekRequested(static_cast<double>(Offset) / 1000000.0);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &TrackId, qlonglong Position)
{
    Q_UNUSED(TrackId);
    emit m_ctrl->setPositionRequested(static_cast<double>(Position) / 1000000.0);
}

void MprisPlayerAdaptor::OpenUri(const QString &Uri)
{
    emit m_ctrl->openUriRequested(Uri);
}

// ------------------------------------------------------------------ controller

MprisController::MprisController(QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::sessionBus())
{
    m_rootAdaptor = new MprisRootAdaptor(this);
    m_playerAdaptor = new MprisPlayerAdaptor(this);

    if (!m_bus.isConnected()) {
        qWarning() << "[mpris] no session bus; media keys disabled";
        return;
    }
    if (!m_bus.registerService(QString::fromLatin1(kService))) {
        qWarning() << "[mpris] service name taken:" << kService;
        return;
    }
    if (!m_bus.registerObject(QString::fromLatin1(kObjectPath), this,
                              QDBusConnection::ExportAdaptors)) {
        qWarning() << "[mpris] failed to register object";
        return;
    }
    m_available = true;
    qDebug() << "[mpris] registered" << kService;
}

MprisController::~MprisController()
{
    if (m_available) {
        m_bus.unregisterObject(QString::fromLatin1(kObjectPath));
        m_bus.unregisterService(QString::fromLatin1(kService));
    }
}

QString MprisController::loopStatus() const
{
    switch (m_repeatMode) {
    case 1: return QStringLiteral("Playlist");
    case 2: return QStringLiteral("Track");
    default: return QStringLiteral("None");
    }
}

void MprisController::emitPropertiesChanged(const QString &iface,
                                             const QVariantMap &changed)
{
    if (!m_available || changed.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createSignal(
        QString::fromLatin1(kObjectPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    msg << iface << QVariant::fromValue(changed) << QStringList();
    m_bus.send(msg);
}

void MprisController::setSource(const QString &path, const QString &title,
                                double durationSeconds, const QString &artUrl)
{
    QVariantMap md;
    md.insert(QStringLiteral("mpris:trackid"),
              QVariant::fromValue(QDBusObjectPath(
                  QStringLiteral("/org/mpris/MediaPlayer2/Track/0"))));
    md.insert(QStringLiteral("mpris:length"),
              QVariant::fromValue<qlonglong>(
                  static_cast<qlonglong>(std::max(0.0, durationSeconds) * 1000000.0)));
    const QUrl url(path);
    const QString uri = url.isValid() && !url.scheme().isEmpty()
        ? url.toString() : QUrl::fromLocalFile(path).toString();
    md.insert(QStringLiteral("xesam:url"), uri);
    md.insert(QStringLiteral("xesam:title"),
              title.isEmpty() ? QFileInfo(path).fileName() : title);
    if (!artUrl.isEmpty())
        md.insert(QStringLiteral("mpris:artUrl"), artUrl);

    m_metadata = md;
    m_positionUs = 0;
    m_hasMedia = true;
    QVariantMap changed;
    changed.insert(QStringLiteral("Metadata"), QVariant::fromValue(m_metadata));
    changed.insert(QStringLiteral("Position"), QVariant::fromValue<qlonglong>(0));
    changed.insert(QStringLiteral("CanGoNext"), true);
    changed.insert(QStringLiteral("CanGoPrevious"), true);
    changed.insert(QStringLiteral("CanPlay"), true);
    changed.insert(QStringLiteral("CanPause"), true);
    changed.insert(QStringLiteral("CanSeek"), m_canSeek);
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface), changed);
}

void MprisController::setPlaybackStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("PlaybackStatus"), m_status}});
}

void MprisController::setPositionSeconds(double seconds)
{
    m_positionUs = static_cast<qlonglong>(std::max(0.0, seconds) * 1000000.0);
}

void MprisController::setVolumePercent(int percent)
{
    const double v = std::clamp(percent / 100.0, 0.0, 2.0);
    if (qFuzzyCompare(m_volume, v))
        return;
    m_volume = v;
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("Volume"), m_volume}});
}

void MprisController::setRate(double rate)
{
    const double r = std::clamp(rate, 0.25, 4.0);
    if (qFuzzyCompare(m_rate, r))
        return;
    m_rate = r;
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("Rate"), m_rate}});
}

void MprisController::setRepeatMode(int mode)
{
    m_repeatMode = std::clamp(mode, 0, 2);
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("LoopStatus"), loopStatus()}});
}

void MprisController::setShuffle(bool on)
{
    m_shuffle = on;
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("Shuffle"), m_shuffle}});
}

void MprisController::setFullscreen(bool on)
{
    m_fullscreen = on;
    emitPropertiesChanged(QString::fromLatin1(kRootIface),
                          {{QStringLiteral("Fullscreen"), m_fullscreen}});
}

void MprisController::setCanSeek(bool on)
{
    m_canSeek = on;
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("CanSeek"), m_canSeek && m_hasMedia}});
}

void MprisController::setHasMedia(bool on)
{
    m_hasMedia = on;
    if (!on) {
        m_metadata.clear();
        m_positionUs = 0;
    }
    QVariantMap changed;
    changed.insert(QStringLiteral("Metadata"), QVariant::fromValue(m_metadata));
    changed.insert(QStringLiteral("CanGoNext"), on);
    changed.insert(QStringLiteral("CanGoPrevious"), on);
    changed.insert(QStringLiteral("CanPlay"), on);
    changed.insert(QStringLiteral("CanPause"), on);
    changed.insert(QStringLiteral("CanSeek"), on && m_canSeek);
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface), changed);
}

void MprisController::emitSeeked(double seconds)
{
    setPositionSeconds(seconds);
    if (!m_available)
        return;
    const qlonglong us = m_positionUs;
    QDBusMessage msg = QDBusMessage::createSignal(
        QString::fromLatin1(kObjectPath),
        QString::fromLatin1(kPlayerIface), QStringLiteral("Seeked"));
    msg << QVariant::fromValue<qlonglong>(us);
    m_bus.send(msg);
    emitPropertiesChanged(QString::fromLatin1(kPlayerIface),
                          {{QStringLiteral("Position"), QVariant::fromValue<qlonglong>(us)}});
}
