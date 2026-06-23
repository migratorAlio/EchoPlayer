#ifndef MPVWIDGET_H
#define MPVWIDGET_H

#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QTimer>
#include <mpv/client.h>

class MpvWidget : public QWidget, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget();

    mpv_handle *handle() const { return m_mpv; }

    void loadFile(const QString &path);
    void play();
    void pause();
    void stop();
    void seek(double pos);
    void setVolume(double vol);
    void setMute(bool mute);
    void setHwDec(const QString &api);
    double timePos() const;
    double duration() const;
    QString videoFormat() const;

signals:
    void positionChanged(double pos);
    void durationChanged(double dur);
    void pausedChanged(bool paused);
    void fileLoaded();
    void playbackFinished();
    void mediaError(const QString &msg);
    void tracksChanged();
    void videoFormatChanged(const QString &fmt);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onMpvEvents();
    void pollChildWindow();

private:
    static void onWakeup(void *ctx);
    void handleEvent(mpv_event *event);
    void deferLoad(const QString &path);
    void setupInputGrabs();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    mpv_handle *m_mpv = nullptr;
    QString m_pendingFile;
    bool m_initialized = false;
    QTimer *m_pollTimer = nullptr;
    unsigned long m_mpvChildWid = 0;
};

#endif // MPVWIDGET_H
