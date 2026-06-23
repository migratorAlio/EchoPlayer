#include "ui/mpvwidget.h"

#include <QDebug>
#include <QMetaObject>
#include <QShowEvent>
#include <QApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

MpvWidget::MpvWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NativeWindow);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &MpvWidget::pollChildWindow);

    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning("mpv_create failed (null handle)");
        return;
    }

    mpv_set_option_string(m_mpv, "video-rotate", "no");
    mpv_set_option_string(m_mpv, "audio", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-builtin-bindings", "no");


    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "video-format", MPV_FORMAT_STRING);

    mpv_set_wakeup_callback(m_mpv, onWakeup, this);

    qApp->installNativeEventFilter(this);
}

MpvWidget::~MpvWidget()
{
    qApp->removeNativeEventFilter(this);
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void MpvWidget::setupInputGrabs()
{
    auto *x11App = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11App) { qDebug() << "[mpv] no X11App"; return; }
    xcb_connection_t *conn = x11App->connection();
    if (!conn) { qDebug() << "[mpv] no xcb conn"; return; }

    xcb_window_t parentWid = static_cast<xcb_window_t>(winId());
    qDebug() << "[mpv] setupInputGrabs parentWid=" << parentWid;

    // Remove any existing grab for this window
    xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, parentWid, XCB_MOD_MASK_ANY);

    // Passive grab: redirect all button+pointer-motion events from parent+children to parent
    // owner_events=False → events go to parentWid instead of the child mpv window
    xcb_grab_button(conn, false, parentWid,
                    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                    parentWid, XCB_NONE,
                    XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);
    xcb_flush(conn);

    qDebug() << "[mpv] passive grab installed on" << parentWid;

    // Also try to find and block mpv's child window (belt and suspenders)
    xcb_query_tree_cookie_t cookie = xcb_query_tree(conn, parentWid);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(conn, cookie, nullptr);
    if (!reply) { qDebug() << "[mpv] no query tree reply"; return; }

    xcb_window_t *children = xcb_query_tree_children(reply);
    int count = xcb_query_tree_children_length(reply);
    qDebug() << "[mpv] tree children count=" << count;

    bool found = false;
    for (int i = 0; i < count; i++) {
        qDebug() << "[mpv] child" << i << "wid=" << children[i];
        if (children[i] != parentWid) {
            m_mpvChildWid = children[i];
            found = true;
        }
    }
    free(reply);

    if (found) {
        m_pollTimer->stop();
        qDebug() << "[mpv] child window" << m_mpvChildWid << "found";
    }
}

void MpvWidget::pollChildWindow()
{
    setupInputGrabs();
}

void MpvWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_initialized || !m_mpv) return;
    m_initialized = true;

    int64_t wid = static_cast<int64_t>(winId());
    mpv_set_option(m_mpv, "wid", MPV_FORMAT_INT64, &wid);

    if (mpv_initialize(m_mpv) < 0) {
        qWarning("mpv_initialize failed");
        return;
    }

    // Poll for child window since mpv creates it asynchronously
    setupInputGrabs();
    m_pollTimer->start(100);

    // Load deferred file
    if (!m_pendingFile.isEmpty()) {
        QString path = m_pendingFile;
        m_pendingFile.clear();
        const QByteArray cpath = path.toUtf8();
        const char *cmd[] = {"loadfile", cpath.constData(), nullptr};
        mpv_command_async(m_mpv, 0, cmd);
    }
}

void MpvWidget::loadFile(const QString &path)
{
    if (!m_mpv) return;
    if (!m_initialized) {
        deferLoad(path);
        return;
    }
    const QByteArray cpath = path.toUtf8();
    const char *cmd[] = {"loadfile", cpath.constData(), nullptr};
    mpv_command_async(m_mpv, 0, cmd);
    // File load triggers child window creation, poll for it
    m_pollTimer->start(100);
}

void MpvWidget::deferLoad(const QString &path)
{
    m_pendingFile = path;
}

void MpvWidget::play()
{
    if (!m_mpv) return;
    int flag = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::pause()
{
    if (!m_mpv) return;
    int flag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::stop()
{
    if (!m_mpv) return;
    const char *cmd[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void MpvWidget::seek(double pos)
{
    if (!m_mpv) return;
    QByteArray posStr = QByteArray::number(pos, 'f', 3);
    const char *cmd[] = {"seek", posStr.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void MpvWidget::setVolume(double vol)
{
    if (!m_mpv) return;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

void MpvWidget::setMute(bool mute)
{
    if (!m_mpv) return;
    int flag = mute ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::setHwDec(const QString &api)
{
    if (!m_mpv) return;
    mpv_set_property_string(m_mpv, "hwdec", api.toUtf8().constData());
}

double MpvWidget::timePos() const
{
    if (!m_mpv) return -1;
    double pos = -1;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return pos;
}

double MpvWidget::duration() const
{
    if (!m_mpv) return 0;
    double dur = 0;
    mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
    return dur;
}

QString MpvWidget::videoFormat() const
{
    if (!m_mpv) return {};
    const char *val = nullptr;
    if (mpv_get_property(m_mpv, "video-format", MPV_FORMAT_STRING, &val) >= 0 && val) {
        QString fmt = QString::fromUtf8(val);
        mpv_free(const_cast<char *>(val));
        return fmt;
    }
    return {};
}

void MpvWidget::onWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onMpvEvents()
{
    if (!m_mpv) return;
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
        // Re-apply input blocking after file load (mpv may recreate child window)
        setupInputGrabs();
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        mpv_event_end_file *ef = static_cast<mpv_event_end_file *>(event->data);
        if (ef->reason == MPV_END_FILE_REASON_EOF)
            emit playbackFinished();
        else if (ef->reason == MPV_END_FILE_REASON_ERROR)
            emit mediaError(ef->error >= 0 ? QString() : mpv_error_string(ef->error));
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = static_cast<mpv_event_property *>(event->data);
        if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            double pos = *static_cast<double *>(prop->data);
            emit positionChanged(pos);
        } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            double dur = *static_cast<double *>(prop->data);
            emit durationChanged(dur);
        } else if (strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
            bool p = *static_cast<int *>(prop->data);
            emit pausedChanged(p);
        } else if (strcmp(prop->name, "eof-reached") == 0 && prop->format == MPV_FORMAT_FLAG) {
        } else if (strcmp(prop->name, "track-list") == 0) {
            emit tracksChanged();
        } else if (strcmp(prop->name, "video-format") == 0 && prop->format == MPV_FORMAT_STRING) {
            const char *val = *static_cast<const char *const *>(prop->data);
            emit videoFormatChanged(val ? QString::fromUtf8(val) : QString());
        }
        break;
    }
    default:
        break;
    }
}

bool MpvWidget::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t")
        return false;

    auto *event = static_cast<xcb_generic_event_t *>(message);
    uint8_t type = event->response_type & ~0x80;

    if (type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE) {
        auto *be = reinterpret_cast<xcb_button_press_event_t *>(event);
        qDebug() << "[mpv NFC] BTN type" << type << "detail" << be->detail << "window=" << be->event;
    } else if (type == XCB_MOTION_NOTIFY) {
        qDebug() << "[mpv NFC] MOTION";
    }

    return false;
}
