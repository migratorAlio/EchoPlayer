#pragma once

#include <QObject>
#include <QList>
#include <QString>

// Calls the desktop's D-Bus screen saver interface while media is playing so
// the screen does not blank (or lock) in the middle of a film. Works with
// freedesktop / GNOME / KDE session services; silently does nothing when no
// service is available (e.g. a bare window manager or headless CI).
class ScreenSaverInhibitor : public QObject
{
    Q_OBJECT
public:
    explicit ScreenSaverInhibitor(QObject *parent = nullptr);
    ~ScreenSaverInhibitor() override;

    void setInhibited(bool on);
    bool inhibited() const { return m_desired; }

private:
    struct Service {
        QString name;
        QString path;
        QString interface;
    };

    void acquire();
    void release();
    static QList<Service> candidates();

    bool m_desired = false;
    bool m_pending = false;
    quint32 m_cookie = 0;
    Service m_active;
};
