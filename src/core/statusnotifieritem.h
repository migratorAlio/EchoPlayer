#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

class QMenu;
class StatusNotifierItem;

// Wire format of org.kde.StatusNotifierItem's IconPixmap property: (iiay),
// ARGB32 in network byte order.
struct SniIconPixmap
{
    int width = 0;
    int height = 0;
    QByteArray data;
};
using SniIconPixmapList = QList<SniIconPixmap>;

QDBusArgument &operator<<(QDBusArgument &arg, const SniIconPixmap &pixmap);
const QDBusArgument &operator>>(const QDBusArgument &arg, SniIconPixmap &pixmap);

struct SniToolTip
{
    QString iconName;
    SniIconPixmapList iconPixmaps;
    QString title;
    QString subtitle;
};

QDBusArgument &operator<<(QDBusArgument &arg, const SniToolTip &tip);
const QDBusArgument &operator>>(const QDBusArgument &arg, SniToolTip &tip);

Q_DECLARE_METATYPE(SniIconPixmap)
Q_DECLARE_METATYPE(SniIconPixmapList)
Q_DECLARE_METATYPE(SniToolTip)

// org.kde.StatusNotifierItem — the interface a tray host (dde-dock, KDE Plasma,
// GNOME's AppIndicator extension) actually listens to.
class SniAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category CONSTANT)
    Q_PROPERTY(QString Id READ id CONSTANT)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(int WindowId READ windowId)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QString IconThemePath READ iconThemePath)
    Q_PROPERTY(SniIconPixmapList IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName)
    Q_PROPERTY(SniIconPixmapList OverlayIconPixmap READ overlayIconPixmap)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(SniIconPixmapList AttentionIconPixmap READ attentionIconPixmap)
    Q_PROPERTY(QString AttentionMovieName READ attentionMovieName)
    Q_PROPERTY(QVariant ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

public:
    explicit SniAdaptor(StatusNotifierItem *parent);

    QString category() const;
    QString id() const;
    QString title() const;
    QString status() const;
    int windowId() const;
    QString iconName() const;
    QString iconThemePath() const { return QString(); }
    SniIconPixmapList iconPixmap() const;
    QString overlayIconName() const;
    SniIconPixmapList overlayIconPixmap() const;
    QString attentionIconName() const;
    SniIconPixmapList attentionIconPixmap() const;
    QString attentionMovieName() const;
    QVariant toolTip() const;
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const;

public slots:
    void Activate(int x, int y);
    void SecondaryActivate(int x, int y);
    void ContextMenu(int x, int y);
    void Scroll(int delta, const QString &orientation);

signals:
    void NewTitle();
    void NewIcon();
    void NewToolTip();
    void NewStatus(const QString &status);

private:
    StatusNotifierItem *m_item = nullptr;
};

// Registers an icon with the StatusNotifierWatcher. Needed on desktops whose
// panel dropped XEmbed (deepin's dde-dock only implements this protocol, so
// QSystemTrayIcon — which is XEmbed-only on X11 — never becomes visible there).
//
// When no watcher is present the caller should fall back to QSystemTrayIcon.
class StatusNotifierItem : public QObject
{
    Q_OBJECT
public:
    explicit StatusNotifierItem(QObject *parent = nullptr);
    ~StatusNotifierItem() override;

    // True when a StatusNotifierWatcher service owns the session bus name.
    static bool isAvailable();

    void setIcon(const QIcon &icon);
    void setIconName(const QString &name);
    void setTitle(const QString &title);
    void setToolTip(const QString &title, const QString &subtitle = QString());
    void setWindowId(int windowId) { m_windowId = windowId; }
    void setMenu(QMenu *menu) { m_menu = menu; }

    bool registered() const { return m_registered; }
    bool registerItem();

    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    int windowId() const { return m_windowId; }
    SniIconPixmapList iconPixmaps() const { return m_pixmaps; }
    QString iconName() const { return m_iconName; }
    QMenu *menu() const { return m_menu; }
    SniAdaptor *adaptor() const { return m_adaptor; }

signals:
    void activateRequested();
    void secondaryActivateRequested();
    void contextMenuRequested(const QPoint &globalPos);
    void scrollRequested(int delta, const QString &orientation);

private:
    friend class SniAdaptor;

    static SniIconPixmapList buildPixmaps(const QIcon &icon);

    QDBusConnection m_bus;
    SniAdaptor *m_adaptor = nullptr;
    QMenu *m_menu = nullptr;
    QString m_serviceName;
    QString m_title = QStringLiteral("EchoPlayer");
    QString m_subtitle;
    QIcon m_icon;
    QString m_iconName;
    SniIconPixmapList m_pixmaps;
    int m_windowId = 0;
    bool m_registered = false;
};
