#pragma once

#include <QOpenGLWidget>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <mpv/client.h>
#include <mpv/render_gl.h>

class MpvWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    mpv_handle *handle() const { return m_mpv; }
    bool mpvReady() const { return m_mpvInitialized; }

    void loadFile(const QString &path);
    void play();
    void pause();
    void stop();
    void seek(double pos, bool absolute = true);
    void frameStep();
    void frameBack();

    void setHwDec(const QString &api);
    void setVolume(double vol);
    void setMute(bool mute);
    void setSpeed(double speed);
    void setAudioTrack(int64_t id);
    void setSubtitleTrack(int64_t id);
    void addSubtitle(const QString &path);
    void setChannelBalance(double balance);
    void setAspect(const QString &aspect);
    void setVideoRotate(int degrees);
    void setSubDelay(double seconds);
    void setSubScale(double scale);
    void setSubPos(int pos);
    void setABLoopA(double pos);
    void setABLoopB(double pos);
    void setAudioFilter(const QString &filter);
    void screenshot(const QString &filePath, bool withSubtitles = true);

    void setProperty(const QString &name, const QVariant &value);
    QVariant property(const QString &name) const;
    void command(const QStringList &args);

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
    void initializeGL() override;
    void paintGL() override;

private slots:
    void onMpvEvents();

private:
    static void onWakeup(void *ctx);
    static void onRenderUpdate(void *ctx);
    static void *getProcAddress(void *ctx, const char *name);
    void handleEvent(mpv_event *event);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_renderCtx = nullptr;
    QString m_pendingFile;
    bool m_mpvInitialized = false;
};
