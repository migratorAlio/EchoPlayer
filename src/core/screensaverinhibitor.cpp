#include "core/screensaverinhibitor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

QList<ScreenSaverInhibitor::Service> ScreenSaverInhibitor::candidates()
{
    return {
        {QStringLiteral("org.freedesktop.ScreenSaver"),
         QStringLiteral("/org/freedesktop/ScreenSaver"),
         QStringLiteral("org.freedesktop.ScreenSaver")},
        {QStringLiteral("org.gnome.ScreenSaver"),
         QStringLiteral("/org/gnome/ScreenSaver"),
         QStringLiteral("org.gnome.ScreenSaver")},
        {QStringLiteral("org.kde.Screensaver"),
         QStringLiteral("/org/kde/Screensaver"),
         QStringLiteral("org.kde.Screensaver")},
    };
}

ScreenSaverInhibitor::ScreenSaverInhibitor(QObject *parent)
    : QObject(parent)
{
}

ScreenSaverInhibitor::~ScreenSaverInhibitor()
{
    release();
}

void ScreenSaverInhibitor::setInhibited(bool on)
{
    if (m_desired == on)
        return;
    m_desired = on;
    if (on)
        acquire();
    else
        release();
}

void ScreenSaverInhibitor::acquire()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || m_pending || m_cookie != 0)
        return;

    for (const Service &s : candidates()) {
        const QDBusReply<bool> registered = bus.interface()->isServiceRegistered(s.name);
        if (!registered.isValid() || !registered.value())
            continue;

        QDBusInterface iface(s.name, s.path, s.interface, bus);
        QDBusPendingCall call = iface.asyncCall(
            QStringLiteral("Inhibit"),
            QStringLiteral("EchoPlayer"), QStringLiteral("Playing media"));
        m_pending = true;
        auto *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, s, watcher]() {
            QDBusPendingReply<quint32> reply = *watcher;
            m_pending = false;
            if (!reply.isError()) {
                m_cookie = reply.value();
                m_active = s;
                qDebug() << "[inhibit] acquired from" << s.name << "cookie" << m_cookie;
                // Paused while the request was in flight: release immediately.
                if (!m_desired)
                    release();
            } else {
                qDebug() << "[inhibit] Inhibit failed:" << reply.error().message();
            }
            watcher->deleteLater();
        });
        return;
    }
    qDebug() << "[inhibit] no screen saver D-Bus service on this session";
}

void ScreenSaverInhibitor::release()
{
    if (m_cookie == 0 || m_active.name.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        m_active.name, m_active.path, m_active.interface,
        QStringLiteral("UnInhibit"));
    msg << QVariant::fromValue<quint32>(m_cookie);
    QDBusConnection::sessionBus().send(msg);
    qDebug() << "[inhibit] released";
    m_cookie = 0;
    m_active = {};
}
