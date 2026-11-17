#include "statusnotifieritem.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDebug>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QTimer>
#include <QSysInfo>
#include <cstring>

QDBusArgument &operator<<(QDBusArgument &arg, const SniIconPixmap &pixmap)
{
    arg.beginStructure();
    arg << pixmap.width << pixmap.height << pixmap.data;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SniIconPixmap &pixmap)
{
    arg.beginStructure();
    arg >> pixmap.width >> pixmap.height >> pixmap.data;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const SniToolTip &tip)
{
    arg.beginStructure();
    arg << tip.iconName << tip.iconPixmaps << tip.title << tip.subtitle;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SniToolTip &tip)
{
    arg.beginStructure();
    arg >> tip.iconName >> tip.iconPixmaps >> tip.title >> tip.subtitle;
    arg.endStructure();
    return arg;
}

SniAdaptor::SniAdaptor(StatusNotifierItem *parent)
    : QDBusAbstractAdaptor(parent), m_item(parent)
{
    setAutoRelaySignals(false);
}

QString SniAdaptor::category() const
{
    return QStringLiteral("ApplicationStatus");
}

QString SniAdaptor::id() const
{
    return QStringLiteral("EchoPlayer");
}

QString SniAdaptor::title() const
{
    static int reads = 0;
    if (reads++ < 6)
        qDebug() << "[tray] host read Title, read #" << reads;
    return m_item->title();
}

QString SniAdaptor::status() const
{
    return QStringLiteral("Active");
}

int SniAdaptor::windowId() const
{
    return m_item->windowId();
}

QString SniAdaptor::iconName() const
{
    return m_item->iconName();
}

SniIconPixmapList SniAdaptor::iconPixmap() const
{
    return m_item->iconPixmaps();
}

QString SniAdaptor::overlayIconName() const { return QString(); }
SniIconPixmapList SniAdaptor::overlayIconPixmap() const { return {}; }
QString SniAdaptor::attentionIconName() const { return QString(); }
SniIconPixmapList SniAdaptor::attentionIconPixmap() const { return {}; }
QString SniAdaptor::attentionMovieName() const { return QString(); }

QVariant SniAdaptor::toolTip() const
{
    SniToolTip tip;
    tip.iconPixmaps = m_item->iconPixmaps();
    tip.title = m_item->title();
    tip.subtitle = m_item->subtitle();
    return QVariant::fromValue(tip);
}

QDBusObjectPath SniAdaptor::menu() const
{
    // No com.canonical.dbusmenu exported: hosts fall back to ContextMenu().
    return QDBusObjectPath(QStringLiteral("/NO_DBUSMENU"));
}

void SniAdaptor::Activate(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    emit m_item->activateRequested();
}

void SniAdaptor::SecondaryActivate(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    emit m_item->secondaryActivateRequested();
}

void SniAdaptor::ContextMenu(int x, int y)
{
    emit m_item->contextMenuRequested(QPoint(x, y));
}

void SniAdaptor::Scroll(int delta, const QString &orientation)
{
    emit m_item->scrollRequested(delta, orientation);
}

StatusNotifierItem::StatusNotifierItem(QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::sessionBus())
{
}

StatusNotifierItem::~StatusNotifierItem()
{
    if (!m_serviceName.isEmpty()) {
        if (m_registered)
            m_bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        m_bus.unregisterService(m_serviceName);
    }
}

bool StatusNotifierItem::isAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    return bus.interface()->isServiceRegistered(QStringLiteral("org.kde.StatusNotifierWatcher"));
}

SniIconPixmapList StatusNotifierItem::buildPixmaps(const QIcon &icon)
{
    SniIconPixmapList pixmaps;
    if (icon.isNull())
        return pixmaps;

    // Panels ask for one of these sizes; hand them pre-scaled bitmaps so a
    // 1075x1054 source never has to be scaled by the host.
    for (int side : {16, 22, 24, 32, 48, 64}) {
        QImage image = icon.pixmap(side, side).toImage().convertToFormat(QImage::Format_ARGB32);
        if (image.isNull())
            continue;
        if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
            // The wire format is ARGB32 in *network* byte order. QImage stores
            // ARGB32 as B,G,R,A on little-endian, so the four bytes must be
            // reversed — a rotate would leave alpha at 0 and the icon would be
            // read but drawn fully transparent.
            for (int row = 0; row < image.height(); ++row) {
                auto *pixel = reinterpret_cast<uint *>(image.scanLine(row));
                uint *end = pixel + image.width();
                while (pixel < end) {
                    *pixel = ((*pixel & 0x000000ffu) << 24)
                           | ((*pixel & 0x0000ff00u) << 8)
                           | ((*pixel & 0x00ff0000u) >> 8)
                           | ((*pixel & 0xff000000u) >> 24);
                    ++pixel;
                }
            }
        }
        SniIconPixmap pixmap;
        pixmap.width = image.width();
        pixmap.height = image.height();
        pixmap.data = QByteArray(reinterpret_cast<const char *>(image.constBits()),
                                 static_cast<int>(image.sizeInBytes()));
        pixmaps.append(pixmap);
    }
    return pixmaps;
}

void StatusNotifierItem::setIcon(const QIcon &icon)
{
    m_icon = icon;
    m_pixmaps = buildPixmaps(icon);
    if (m_registered && m_adaptor)
        emit m_adaptor->NewIcon();
}

void StatusNotifierItem::setIconName(const QString &name)
{
    m_iconName = name;
    if (m_registered && m_adaptor)
        emit m_adaptor->NewIcon();
}

void StatusNotifierItem::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    if (m_registered && m_adaptor) {
        emit m_adaptor->NewTitle();
        emit m_adaptor->NewToolTip();
    }
}

void StatusNotifierItem::setToolTip(const QString &title, const QString &subtitle)
{
    m_title = title;
    m_subtitle = subtitle;
    if (m_registered && m_adaptor)
        emit m_adaptor->NewToolTip();
}

bool StatusNotifierItem::registerItem()
{
    if (m_registered)
        return true;
    if (!isAvailable())
        return false;

    qDBusRegisterMetaType<SniIconPixmap>();
    qDBusRegisterMetaType<SniIconPixmapList>();
    qDBusRegisterMetaType<SniToolTip>();

    m_serviceName = QStringLiteral("org.kde.StatusNotifierItem-%1-1")
                        .arg(QCoreApplication::applicationPid());
    if (!m_bus.registerService(m_serviceName)) {
        qWarning() << "[tray] cannot own" << m_serviceName;
        return false;
    }

    if (!m_adaptor)
        m_adaptor = new SniAdaptor(this);
    if (!m_bus.registerObject(QStringLiteral("/StatusNotifierItem"), this,
                              QDBusConnection::ExportAdaptors)) {
        qWarning() << "[tray] cannot export /StatusNotifierItem";
        m_bus.unregisterService(m_serviceName);
        return false;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("RegisterStatusNotifierItem"));
    call << m_serviceName;
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, 1500);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "[tray] RegisterStatusNotifierItem failed:" << reply.errorMessage();
        m_bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        m_bus.unregisterService(m_serviceName);
        return false;
    }

    m_registered = true;
    qDebug() << "[tray] registered StatusNotifierItem as" << m_serviceName
             << "pixmaps:" << m_pixmaps.size();

    // Some hosts build their tray surface lazily and only react to the
    // change notifications. Nudge them once properties are in place.
    QTimer::singleShot(300, this, [this] {
        if (!m_adaptor)
            return;
        emit m_adaptor->NewTitle();
        emit m_adaptor->NewIcon();
        emit m_adaptor->NewToolTip();
        emit m_adaptor->NewStatus(QStringLiteral("Active"));
    });
    return true;
}
