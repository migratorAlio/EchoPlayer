#include "ui/mpvwidget.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QtGui/qguiapplication_platform.h>

#include <cstring>

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    setFormat(fmt);

    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning("[mpv] mpv_create failed");
        return;
    }

    // Rendering is driven by mpv_render_context_render() from paintGL().
    // No --wid, no X11 child window, no xcb event grabs: works on X11 and Wayland.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-builtin-bindings", "no");
    mpv_set_option_string(m_mpv, "video-rotate", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "keep-open", "no");
    mpv_set_option_string(m_mpv, "sub-auto", "exact");
    mpv_set_option_string(m_mpv, "audio-file-auto", "exact");

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "video-format", MPV_FORMAT_STRING);

    mpv_set_wakeup_callback(m_mpv, onWakeup, this);
}

MpvWidget::~MpvWidget()
{
    makeCurrent();
    if (m_renderCtx) {
        mpv_render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
        mpv_render_context_free(m_renderCtx);
        m_renderCtx = nullptr;
    }
    doneCurrent();
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void *MpvWidget::getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(name));
}

void MpvWidget::initializeGL()
{
    if (!m_mpv)
        return;

    if (!m_mpvInitialized) {
        if (mpv_initialize(m_mpv) < 0) {
            qWarning("[mpv] mpv_initialize failed");
            return;
        }
        m_mpvInitialized = true;
    }

    if (m_renderCtx)
        return;

    mpv_opengl_init_params glInit{&MpvWidget::getProcAddress, this};

    // Supply the native display so VAAPI/EGL hardware decode can use
    // zero-copy interop on both X11 and Wayland sessions.
    bool haveDisplay = false;
    mpv_render_param displayParam{MPV_RENDER_PARAM_INVALID, nullptr};
#if QT_CONFIG(xcb)
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
        displayParam = {MPV_RENDER_PARAM_X11_DISPLAY, x11->display()};
        haveDisplay = true;
    }
#endif
#if QT_CONFIG(wayland)
    if (!haveDisplay) {
        if (auto *wl = qApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            displayParam = {MPV_RENDER_PARAM_WL_DISPLAY, wl->display()};
            haveDisplay = true;
        }
    }
#endif
    Q_UNUSED(haveDisplay)

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        displayParam,
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&m_renderCtx, m_mpv, params) < 0) {
        qWarning("[mpv] failed to create OpenGL render context");
        m_renderCtx = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(m_renderCtx, onRenderUpdate, this);
    qDebug() << "[mpv] OpenGL render context ready" << (haveDisplay ? "(native display supplied)" : "");

    if (!m_pendingFile.isEmpty()) {
        const QString path = m_pendingFile;
        m_pendingFile.clear();
        loadFile(path);
    }
}

void MpvWidget::paintGL()
{
    if (!m_renderCtx)
        return;

    const qreal dpr = devicePixelRatioF();
    const int w = qMax(1, static_cast<int>(width() * dpr));
    const int h = qMax(1, static_cast<int>(height() * dpr));

    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()), w, h, 0};
    int flip = 1; // OpenGL default framebuffer has a flipped Y axis
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(m_renderCtx, params);
    mpv_render_context_report_swap(m_renderCtx);
}

void MpvWidget::onRenderUpdate(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "update", Qt::QueuedConnection);
}

void MpvWidget::onWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onMpvEvents()
{
    if (!m_mpv)
        return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
}

void MpvWidget::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_FILE_LOADED:
        qDebug() << "[mpv] file loaded";
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        auto *ef = static_cast<mpv_event_end_file *>(event->data);
        if (ef && ef->reason == MPV_END_FILE_REASON_EOF) {
            qDebug() << "[mpv] playback reached EOF";
            emit playbackFinished();
        }
        else if (ef && ef->reason == MPV_END_FILE_REASON_ERROR)
            emit mediaError(ef->error < 0 ? QString::fromUtf8(mpv_error_string(ef->error))
                                          : QStringLiteral("Playback error"));
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        if (!prop || !prop->name)
            break;
        if (std::strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            emit positionChanged(*static_cast<double *>(prop->data));
        } else if (std::strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            emit durationChanged(*static_cast<double *>(prop->data));
        } else if (std::strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
            emit pausedChanged(*static_cast<int *>(prop->data) != 0);
        } else if (std::strcmp(prop->name, "track-list") == 0) {
            emit tracksChanged();
        } else if (std::strcmp(prop->name, "video-format") == 0) {
            const char *val = prop->format == MPV_FORMAT_STRING
                                  ? *static_cast<const char *const *>(prop->data) : nullptr;
            emit videoFormatChanged(val ? QString::fromUtf8(val) : QString());
        }
        break;
    }
    default:
        break;
    }
}

void MpvWidget::loadFile(const QString &path)
{
    if (!m_mpv)
        return;
    if (!m_mpvInitialized || !m_renderCtx) {
        m_pendingFile = path;
        return;
    }
    command({"loadfile", path, "replace"});
}

void MpvWidget::play()
{
    setProperty("pause", false);
}

void MpvWidget::pause()
{
    setProperty("pause", true);
}

void MpvWidget::stop()
{
    command({"stop"});
}

void MpvWidget::seek(double pos, bool absolute)
{
    command({"seek", QString::number(pos, 'f', 3), absolute ? "absolute" : "relative"});
}

void MpvWidget::frameStep()
{
    command({"frame-step"});
}

void MpvWidget::frameBack()
{
    command({"frame-back-step"});
}

void MpvWidget::setHwDec(const QString &api)
{
    setProperty("hwdec", api);
}

void MpvWidget::setVolume(double vol)
{
    setProperty("volume", vol);
}

void MpvWidget::setMute(bool mute)
{
    setProperty("mute", mute);
}

void MpvWidget::setSpeed(double speed)
{
    setProperty("speed", speed);
}

void MpvWidget::setAudioTrack(int64_t id)
{
    if (id < 0)
        setProperty("aid", QStringLiteral("no"));
    else
        setProperty("aid", QVariant::fromValue<qlonglong>(id));
}

void MpvWidget::setSubtitleTrack(int64_t id)
{
    if (id < 0)
        setProperty("sid", QStringLiteral("no"));
    else
        setProperty("sid", QVariant::fromValue<qlonglong>(id));
}

void MpvWidget::addSubtitle(const QString &path)
{
    command({"sub-add", path, "auto"});
}

void MpvWidget::setChannelBalance(double balance)
{
    setProperty("balance", balance);
}

void MpvWidget::setAspect(const QString &aspect)
{
    setProperty("video-aspect-override", aspect);
}

void MpvWidget::setVideoRotate(int degrees)
{
    setProperty("video-rotate", QVariant::fromValue<qlonglong>(degrees));
}

void MpvWidget::setSubDelay(double seconds)
{
    setProperty("sub-delay", seconds);
}

void MpvWidget::setSubScale(double scale)
{
    setProperty("sub-scale", scale);
}

void MpvWidget::setSubPos(int pos)
{
    setProperty("sub-pos", QVariant::fromValue<qlonglong>(pos));
}

void MpvWidget::setABLoopA(double pos)
{
    setProperty("ab-loop-a", pos);
}

void MpvWidget::setABLoopB(double pos)
{
    setProperty("ab-loop-b", pos);
}

void MpvWidget::setAudioFilter(const QString &filter)
{
    setProperty("af", filter);
}

void MpvWidget::screenshot(const QString &filePath, bool withSubtitles)
{
    command({"screenshot-to-file", filePath, withSubtitles ? "subtitles" : "video"});
}

void MpvWidget::setProperty(const QString &name, const QVariant &value)
{
    if (!m_mpv)
        return;
    const QByteArray n = name.toUtf8();
    const char *key = n.constData();

    switch (value.typeId()) {
    case QMetaType::Bool: {
        int v = value.toBool() ? 1 : 0;
        mpv_set_property(m_mpv, key, MPV_FORMAT_FLAG, &v);
        break;
    }
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong: {
        int64_t v = value.toLongLong();
        mpv_set_property(m_mpv, key, MPV_FORMAT_INT64, &v);
        break;
    }
    case QMetaType::Double:
    case QMetaType::Float: {
        double v = value.toDouble();
        mpv_set_property(m_mpv, key, MPV_FORMAT_DOUBLE, &v);
        break;
    }
    default: {
        const QByteArray s = value.toString().toUtf8();
        const char *v = s.constData();
        mpv_set_property(m_mpv, key, MPV_FORMAT_STRING, &v);
        break;
    }
    }
}

QVariant MpvWidget::property(const QString &name) const
{
    if (!m_mpv)
        return {};
    const QByteArray n = name.toUtf8();
    const char *key = n.constData();

    double d = 0;
    if (mpv_get_property(m_mpv, key, MPV_FORMAT_DOUBLE, &d) >= 0)
        return d;
    int64_t i = 0;
    if (mpv_get_property(m_mpv, key, MPV_FORMAT_INT64, &i) >= 0)
        return QVariant::fromValue<qlonglong>(i);
    int f = 0;
    if (mpv_get_property(m_mpv, key, MPV_FORMAT_FLAG, &f) >= 0)
        return f != 0;
    char *s = nullptr;
    if (mpv_get_property(m_mpv, key, MPV_FORMAT_STRING, &s) >= 0 && s) {
        const QString r = QString::fromUtf8(s);
        mpv_free(s);
        return r;
    }
    return {};
}

void MpvWidget::command(const QStringList &args)
{
    if (!m_mpv || args.isEmpty())
        return;
    QList<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString &a : args)
        storage.append(a.toUtf8());

    QVector<const char *> argv;
    argv.reserve(storage.size() + 1);
    for (const QByteArray &a : storage)
        argv.append(a.constData());
    argv.append(nullptr);

    mpv_command_async(m_mpv, 0, argv.data());
}

double MpvWidget::timePos() const
{
    return property("time-pos").toDouble();
}

double MpvWidget::duration() const
{
    return property("duration").toDouble();
}

QString MpvWidget::videoFormat() const
{
    return property("video-format").toString();
}
