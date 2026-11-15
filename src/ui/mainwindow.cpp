#include "ui/mainwindow.h"
#include "ui/mpvwidget.h"
#include "ui/titlebar.h"
#include "ui/controlbar.h"
#include "ui/spectrumwidget.h"
#include "core/audioengine.h"
#include "core/mpriscontroller.h"
#include "core/screensaverinhibitor.h"
#include "ui/thumbnailprovider.h"
#include "ui/adjustpanel.h"
#include "i18n/languagemanager.h"
#include "playlist/playlistprotocol.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStyle>
#include <QStringConverter>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QCryptographicHash>
#include <QRandomGenerator>

#include <mpv/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct TrackEntry {
    qlonglong id = -1;
    QString label;
};

QList<TrackEntry> mpvTracks(mpv_handle *h, const char *type)
{
    QList<TrackEntry> out;
    if (!h)
        return out;
    mpv_node node{};
    if (mpv_get_property(h, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return out;
    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node &item = node.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP || !item.u.list)
                continue;
            bool match = false;
            bool external = false;
            QString lang, title;
            qlonglong id = -1;
            for (int j = 0; j < item.u.list->num; ++j) {
                const char *k = item.u.list->keys[j];
                const mpv_node &v = item.u.list->values[j];
                if (!k) continue;
                if (std::strcmp(k, "type") == 0 && v.format == MPV_FORMAT_STRING)
                    match = std::strcmp(v.u.string, type) == 0;
                else if (std::strcmp(k, "lang") == 0 && v.format == MPV_FORMAT_STRING)
                    lang = QString::fromUtf8(v.u.string);
                else if (std::strcmp(k, "title") == 0 && v.format == MPV_FORMAT_STRING)
                    title = QString::fromUtf8(v.u.string);
                else if (std::strcmp(k, "id") == 0 && v.format == MPV_FORMAT_INT64)
                    id = static_cast<qlonglong>(v.u.int64);
                else if (std::strcmp(k, "external") == 0 && v.format == MPV_FORMAT_FLAG)
                    external = v.u.flag != 0;
            }
            if (match && id >= 0) {
                QString label = title.isEmpty()
                    ? (lang.isEmpty() ? QStringLiteral("Track %1").arg(id) : languageName(lang))
                    : title;
                if (external)
                    label += QCoreApplication::translate("MainWindow", " (外挂)");
                out.append({id, label});
            }
        }
    }
    mpv_free_node_contents(&node);
    return out;
}

QString readTextAutoDetect(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray raw = f.readAll();

    QStringDecoder dec(QStringConverter::Utf8);
    QString result = dec.decode(raw);
    if (!dec.hasError()) {
        result.remove(QChar('\r'));
        return result;
    }
    QStringDecoder decGb("GB18030");
    if (decGb.isValid()) {
        result = decGb.decode(raw);
        result.remove(QChar('\r'));
        return result;
    }
    QStringDecoder decSys(QStringConverter::System);
    result = decSys.decode(raw);
    result.remove(QChar('\r'));
    return result;
}

// Menus are created once with tr() at startup. Keep the original source text on
// each action so a later language switch can re-translate it through the
// installed QTranslator instead of leaving stale Chinese labels behind.
QString retranslateOf(const QObject *obj)
{
    const QString src = obj->property("trSource").toString();
    if (src.isEmpty())
        return {};
    const QByteArray ctx = obj->property("trContext").toByteArray();
    const QByteArray source = src.toUtf8();
    return QCoreApplication::translate(ctx.constData(), source.constData());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_mpvWidget = new MpvWidget(this);
    m_audioEngine = new AudioEngine(this);
    m_titleAnim = new QPropertyAnimation(this);
    m_ctrlAnim = new QPropertyAnimation(this);

    m_spectrumTimer = new QTimer(this);
    m_spectrumTimer->setInterval(30);
    m_lyricTimer = new QTimer(this);
    m_lyricTimer->setInterval(100);
    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(50);
    m_osdTimer = new QTimer(this);
    m_osdTimer->setSingleShot(true);

    setupUI();
    setupMenus();
    setupShortcuts();
    connectSignals();
    loadSettings();
    setupAdjustPanel();
    setupSeekPreview();
    setupDesktopIntegration();
    loadPlaylistState();
    // Captures the tray menu too, which is built in setupDesktopIntegration().
    captureTranslatables();
}

MainWindow::~MainWindow()
{
    m_destroyed = true;
    closePlaylist();
    m_audioEngine->closeAll();
}

void MainWindow::setupUI()
{
    setWindowTitle(QStringLiteral("EchoPlayer"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAcceptDrops(true);

    auto *central = new QWidget(this);
    central->setMouseTracking(true);
    central->installEventFilter(this);

    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_mpvWidget->installEventFilter(this);
    layout->addWidget(m_mpvWidget, 1);

    m_spectrumWidget = new SpectrumWidget(central);
    m_spectrumWidget->hide();
    layout->addWidget(m_spectrumWidget, 0);

    m_titleBar = new TitleBar(central);
    m_ctrlBar = new ControlBar(central);
    m_ctrlBar->prevBtn()->installEventFilter(this);
    m_ctrlBar->nextBtn()->installEventFilter(this);
    m_ctrlBar->installEventFilter(this);

    m_lyricLabel = new QLabel(central);
    m_lyricLabel->setAlignment(Qt::AlignCenter);
    m_lyricLabel->setWordWrap(true);
    m_lyricLabel->setStyleSheet(
        "color: white; font-size: 20px; font-weight: bold;"
        "background: rgba(0,0,0,140); padding: 6px 16px; border-radius: 8px;");
    m_lyricLabel->setVisible(false);

    m_posterLabel = new QLabel(central);
    m_posterLabel->setAlignment(Qt::AlignCenter);
    m_posterLabel->setStyleSheet("background: transparent;");
    m_posterLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_posterLabel->setVisible(false);

    m_osdLabel = new QLabel(central);
    m_osdLabel->setAlignment(Qt::AlignCenter);
    m_osdLabel->setStyleSheet(
        "color: white; font-size: 15px; background: rgba(0,0,0,170);"
        "padding: 8px 18px; border-radius: 6px;");
    m_osdLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osdLabel->setVisible(false);

    m_infoLabel = new QLabel(central);
    m_infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_infoLabel->setStyleSheet(
        "color: #eaeaea; font-family: monospace; font-size: 12px;"
        "background: rgba(0,0,0,190); padding: 12px; border-radius: 6px;");
    m_infoLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_infoLabel->setVisible(false);

    setCentralWidget(central);
    resize(960, 640);
    central->layout()->activate();

    m_titleBar->raise();
    m_ctrlBar->raise();
    repositionBars();
}

void MainWindow::setupMenus()
{
    m_contextMenu = new QMenu(this);

    // ---- Subtitles ----
    m_subMenu = m_contextMenu->addMenu(tr("字幕"));
    m_subTrackMenu = m_subMenu->addMenu(tr("选择字幕"));
    m_subGroup = new QActionGroup(this);
    m_subGroup->setExclusive(true);

    m_subMenu->addSeparator();
    connect(m_subMenu->addAction(tr("加载外部字幕...")), &QAction::triggered,
            this, &MainWindow::onAddSubtitle);
    connect(m_subMenu->addAction(tr("提前 0.1 秒 (Z)")), &QAction::triggered,
            this, [this] { adjustSubDelay(-0.1); });
    connect(m_subMenu->addAction(tr("延后 0.1 秒 (X)")), &QAction::triggered,
            this, [this] { adjustSubDelay(0.1); });
    connect(m_subMenu->addAction(tr("字幕放大")), &QAction::triggered, this, [this] {
        if (!m_adjustPanel) return;
        m_adjustPanel->bumpSubScale(0.1);
        showOsd(tr("字幕大小 %1").arg(QString::number(m_adjustPanel->subScale(), 'f', 1)));
    });
    connect(m_subMenu->addAction(tr("字幕缩小")), &QAction::triggered, this, [this] {
        if (!m_adjustPanel) return;
        m_adjustPanel->bumpSubScale(-0.1);
        showOsd(tr("字幕大小 %1").arg(QString::number(m_adjustPanel->subScale(), 'f', 1)));
    });

    // ---- Video ----
    m_videoMenu = m_contextMenu->addMenu(tr("视频"));
    m_decoderMenu = m_videoMenu->addMenu(tr("硬件解码"));
    m_decoderGroup = new QActionGroup(this);
    m_decoderGroup->setExclusive(true);
    auto addDec = [this](const QString &label, const QString &api) {
        QAction *a = m_decoderMenu->addAction(label);
        a->setCheckable(true);
        a->setData(api);
        m_decoderGroup->addAction(a);
    };
    addDec(tr("关闭"), "no");
    addDec(tr("自动"), "auto");
    addDec(QStringLiteral("CUDA"), "cuda");
    addDec(QStringLiteral("VAAPI"), "vaapi");
    addDec(QStringLiteral("VDPAU"), "vdpau");
    addDec(QStringLiteral("Vulkan"), "vulkan");
    m_decoderMenu->actions().first()->setChecked(true);

    m_aspectMenu = m_videoMenu->addMenu(tr("画面比例"));
    m_aspectGroup = new QActionGroup(this);
    m_aspectGroup->setExclusive(true);
    auto addAspect = [this](const QString &label, const QString &value) {
        QAction *a = m_aspectMenu->addAction(label);
        a->setCheckable(true);
        a->setData(value);
        m_aspectGroup->addAction(a);
    };
    addAspect(tr("原始 (1)"), "-1");
    addAspect(tr("16:9 (2)"), "16:9");
    addAspect(tr("4:3 (3)"), "4:3");
    addAspect(tr("2.35:1 (4)"), "2.35:1");
    m_aspectMenu->actions().first()->setChecked(true);

    connect(m_videoMenu->addAction(tr("旋转 90° (R)")), &QAction::triggered,
            this, [this] { rotateVideo(); });
    connect(m_videoMenu->addAction(tr("亮度 + (B)")), &QAction::triggered,
            this, [this] { adjustBrightness(5); });
    connect(m_videoMenu->addAction(tr("亮度 - (N)")), &QAction::triggered,
            this, [this] { adjustBrightness(-5); });
    connect(m_videoMenu->addAction(tr("重置画面")), &QAction::triggered,
            this, [this] { resetPicture(); });

    // ---- Audio ----
    m_audioTopMenu = m_contextMenu->addMenu(tr("声音"));
    m_audioMenu = m_audioTopMenu->addMenu(tr("选择音轨"));
    m_audioGroup = new QActionGroup(this);
    m_audioGroup->setExclusive(true);

    m_channelMenu = m_audioTopMenu->addMenu(tr("声道"));
    m_channelModeGroup = new QActionGroup(this);
    m_channelModeGroup->setExclusive(true);
    auto addCh = [this](const QString &label, int mode) {
        QAction *a = m_channelMenu->addAction(label);
        a->setCheckable(true);
        a->setData(mode);
        m_channelModeGroup->addAction(a);
    };
    addCh(tr("立体声"), 0);
    addCh(tr("偏左声道"), 1);
    addCh(tr("偏右声道"), 2);
    m_channelMenu->actions().first()->setChecked(true);

    m_presetMenu = m_audioTopMenu->addMenu(tr("音效 (E)"));
    m_presetGroup = new QActionGroup(this);
    m_presetGroup->setExclusive(true);
    const QString presets[] = {tr("关闭"), tr("低音增强"), tr("高音增强"), tr("人声增强")};
    for (int i = 0; i < 4; ++i) {
        QAction *a = m_presetMenu->addAction(presets[i]);
        a->setCheckable(true);
        a->setData(i);
        m_presetGroup->addAction(a);
    }
    m_presetMenu->actions().first()->setChecked(true);

    // ---- Playback ----
    QMenu *playMenu = m_contextMenu->addMenu(tr("播放"));
    connect(playMenu->addAction(tr("减速 ([)")), &QAction::triggered,
            this, [this] { setSpeed(m_speed - 0.1); });
    connect(playMenu->addAction(tr("加速 (])")), &QAction::triggered,
            this, [this] { setSpeed(m_speed + 0.1); });
    connect(playMenu->addAction(tr("恢复正常速度")), &QAction::triggered,
            this, [this] { setSpeed(1.0); });
    playMenu->addSeparator();
    connect(playMenu->addAction(tr("逐帧前进 (.)")), &QAction::triggered,
            this, [this] { m_mpvWidget->frameStep(); });
    connect(playMenu->addAction(tr("设置 A/B 循环点 (A)")), &QAction::triggered,
            this, [this] { markABLoop(); });
    connect(playMenu->addAction(tr("清除 A/B 循环")), &QAction::triggered, this, [this] {
        m_abA = -1.0;
        m_mpvWidget->setABLoopA(-1.0);
        m_mpvWidget->setABLoopB(-1.0);
        showOsd(tr("A/B 循环已关闭"));
    });

    // ---- Tools ----
    QMenu *tools = m_contextMenu->addMenu(tr("工具"));
    connect(tools->addAction(tr("打开文件...")), &QAction::triggered,
            this, &MainWindow::onOpenFile);
    connect(tools->addAction(tr("截图 (S)")), &QAction::triggered,
            this, [this] { screenshot(); });
    connect(tools->addAction(tr("媒体信息 (I)")), &QAction::triggered,
            this, [this] { toggleMediaInfo(); });
    connect(tools->addAction(tr("调整面板... (Ctrl+P)")), &QAction::triggered,
            this, [this] { toggleAdjustPanel(); });
    connect(tools->addAction(tr("打开网络流... (Ctrl+U)")), &QAction::triggered,
            this, &MainWindow::onOpenUrl);
    connect(tools->addAction(tr("显示 / 隐藏播放列表")), &QAction::triggered,
            this, &MainWindow::onTogglePlaylist);

    QMenu *listMenu = tools->addMenu(tr("播放列表"));
    connect(listMenu->addAction(tr("添加文件到列表...")), &QAction::triggered,
            this, &MainWindow::onAddFilesToPlaylist);
    connect(listMenu->addAction(tr("清空列表")), &QAction::triggered,
            this, &MainWindow::onClearPlaylist);
    listMenu->addSeparator();
    m_shuffleAction = listMenu->addAction(tr("随机播放 (Ctrl+R)"));
    m_shuffleAction->setCheckable(true);
    connect(m_shuffleAction, &QAction::triggered, this, &MainWindow::onToggleShuffle);
    m_repeatAction = listMenu->addAction(tr("循环：不循环"));
    connect(m_repeatAction, &QAction::triggered, this, &MainWindow::onCycleRepeat);

    tools->addSeparator();
    m_trayOnCloseAction = tools->addAction(tr("关闭时最小化到托盘"));
    m_trayOnCloseAction->setCheckable(true);
    m_trayOnCloseAction->setChecked(true);
    connect(m_trayOnCloseAction, &QAction::toggled, this, [this](bool on) {
        m_trayOnClose = on;
    });
    captureTranslatables();
}

void MainWindow::setupShortcuts()
{
    auto bind = [this](int key, auto fn) {
        QAction *a = new QAction(this);
        a->setShortcut(QKeySequence(key));
        a->setShortcutContext(Qt::ApplicationShortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, fn);
    };

    bind(Qt::Key_Space, [this] { onPlayPause(); });
    bind(Qt::Key_K, [this] { onPlayPause(); });
    bind(Qt::Key_Left, [this] { seekRelative(-5); });
    bind(Qt::Key_Right, [this] { seekRelative(5); });
    bind(static_cast<int>(Qt::ShiftModifier) | static_cast<int>(Qt::Key_Left),
         [this] { seekRelative(-30); });
    bind(static_cast<int>(Qt::ShiftModifier) | static_cast<int>(Qt::Key_Right),
         [this] { seekRelative(30); });
    bind(Qt::Key_Up, [this] { adjustVolume(5); });
    bind(Qt::Key_Down, [this] { adjustVolume(-5); });
    bind(Qt::Key_M, [this] { onToggleMute(); });
    bind(Qt::Key_F, [this] { onToggleFullscreen(); });
    bind(Qt::Key_Return, [this] { onToggleFullscreen(); });
    bind(Qt::Key_Escape, [this] { if (isFullScreen()) onToggleFullscreen(); });
    bind(Qt::Key_BracketLeft, [this] { setSpeed(m_speed - 0.1); });
    bind(Qt::Key_BracketRight, [this] { setSpeed(m_speed + 0.1); });
    bind(Qt::Key_Backspace, [this] { setSpeed(1.0); });
    bind(Qt::Key_Comma, [this] { m_mpvWidget->frameBack(); });
    bind(Qt::Key_Period, [this] { m_mpvWidget->frameStep(); });
    bind(Qt::Key_S, [this] { screenshot(); });
    bind(Qt::Key_PageUp, [this] { onPrevTrack(); });
    bind(Qt::Key_PageDown, [this] { onNextTrack(); });
    bind(Qt::Key_Z, [this] { adjustSubDelay(-0.1); });
    bind(Qt::Key_X, [this] { adjustSubDelay(0.1); });
    bind(Qt::Key_A, [this] { markABLoop(); });
    bind(Qt::Key_I, [this] { toggleMediaInfo(); });
    bind(Qt::Key_1, [this] { setAspect("-1"); });
    bind(Qt::Key_2, [this] { setAspect("16:9"); });
    bind(Qt::Key_3, [this] { setAspect("4:3"); });
    bind(Qt::Key_4, [this] { setAspect("2.35:1"); });
    bind(Qt::Key_R, [this] { rotateVideo(); });
    bind(Qt::Key_B, [this] { adjustBrightness(5); });
    bind(Qt::Key_N, [this] { adjustBrightness(-5); });
    bind(Qt::Key_E, [this] { cycleAudioPreset(); });
    bind(Qt::Key_L, [this] { onCycleRepeat(); });
    bind(Qt::Key_H, [this] { hide(); if (m_trayIcon) m_trayIcon->show(); });
    // Ctrl+O is already provided by TitleBar's open action (QKeySequence::Open).
    bind(static_cast<int>(Qt::ControlModifier) | static_cast<int>(Qt::Key_P),
         [this] { toggleAdjustPanel(); });
    bind(static_cast<int>(Qt::ControlModifier) | static_cast<int>(Qt::Key_U),
         [this] { onOpenUrl(); });
    bind(static_cast<int>(Qt::ControlModifier) | static_cast<int>(Qt::Key_R),
         [this] { onToggleShuffle(); });
    bind(static_cast<int>(Qt::ControlModifier) | static_cast<int>(Qt::Key_Q),
         [this] { close(); });
}

void MainWindow::setupAdjustPanel()
{
    // m_settings exists by now (loadSettings() ran first).
    m_adjustPanel = new AdjustPanel(m_mpvWidget, m_settings, this);
    connect(m_adjustPanel, &AdjustPanel::presetOverridden, this, [this] {
        m_presetIndex = 0;
        const QList<QAction *> acts = m_presetGroup->actions();
        if (!acts.isEmpty())
            acts.first()->setChecked(true);
    });
}

void MainWindow::toggleAdjustPanel()
{
    if (!m_adjustPanel)
        return;
    if (m_adjustPanel->isVisible()) {
        m_adjustPanel->hide();
    } else {
        m_adjustPanel->show();
        m_adjustPanel->raise();
        m_adjustPanel->activateWindow();
    }
}

void MainWindow::captureTranslatables()
{
    const QList<QWidget *> roots = {m_contextMenu, m_trayMenu};
    for (QWidget *root : roots) {
        if (!root)
            continue;
        for (QAction *a : root->findChildren<QAction *>()) {
            if (!a->text().isEmpty() && a->property("trSource").toString().isEmpty()) {
                a->setProperty("trSource", a->text());
                a->setProperty("trContext", QByteArrayLiteral("MainWindow"));
            }
        }
        for (QMenu *menu : root->findChildren<QMenu *>()) {
            if (!menu->title().isEmpty() && menu->property("trSource").toString().isEmpty()) {
                menu->setProperty("trSource", menu->title());
                menu->setProperty("trContext", QByteArrayLiteral("MainWindow"));
            }
        }
    }
}

void MainWindow::retranslateDynamic()
{
    for (QAction *a : findChildren<QAction *>()) {
        const QString text = retranslateOf(a);
        if (!text.isEmpty())
            a->setText(text);
    }
    for (QMenu *menu : findChildren<QMenu *>()) {
        const QString title = retranslateOf(menu);
        if (!title.isEmpty())
            menu->setTitle(title);
    }
    // This label contains a runtime value, so it cannot go through trSource.
    if (m_repeatAction)
        m_repeatAction->setText(tr("循环：%1").arg(repeatModeName()));
}

void MainWindow::connectSignals()
{
    connect(m_titleBar, &TitleBar::openFileRequested, this, &MainWindow::onOpenFile);
    connect(m_titleBar, &TitleBar::fullscreenRequested, this, &MainWindow::onToggleFullscreen);
    connect(m_titleBar, &TitleBar::languageSwitchRequested,
            this, &MainWindow::onLanguageSwitch);

    connect(m_ctrlBar, &ControlBar::playPauseClicked, this, &MainWindow::onPlayPause);
    connect(m_ctrlBar, &ControlBar::stopClicked, this, &MainWindow::onStop);
    connect(m_ctrlBar, &ControlBar::rewindClicked, this, &MainWindow::onRewind);
    connect(m_ctrlBar, &ControlBar::forwardClicked, this, &MainWindow::onForward);
    connect(m_ctrlBar, &ControlBar::openFileClicked, this, &MainWindow::onOpenFile);
    connect(m_ctrlBar, &ControlBar::listClicked, this, &MainWindow::onTogglePlaylist);
    connect(m_ctrlBar->seekSlider(), &QSlider::sliderPressed, this, &MainWindow::onSliderPressed);
    connect(m_ctrlBar->seekSlider(), &QSlider::sliderReleased, this, &MainWindow::onSliderReleased);
    connect(m_ctrlBar->seekSlider(), &QSlider::sliderMoved, this, &MainWindow::onSliderMoved);
    connect(m_ctrlBar->volumeSlider(), &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_ctrlBar->volBtn(), &QToolButton::clicked, this, &MainWindow::onToggleMute);

    connect(m_mpvWidget, &MpvWidget::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_mpvWidget, &MpvWidget::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_mpvWidget, &MpvWidget::pausedChanged, this, &MainWindow::onPausedChanged);
    connect(m_mpvWidget, &MpvWidget::fileLoaded, this, &MainWindow::onFileLoaded);
    connect(m_mpvWidget, &MpvWidget::playbackFinished, this, &MainWindow::onPlaybackFinished);
    connect(m_mpvWidget, &MpvWidget::mediaError, this, &MainWindow::onMediaError);
    connect(m_mpvWidget, &MpvWidget::tracksChanged, this, [this] {
        populateSubMenu();
        populateAudioMenu();
    });
    connect(m_mpvWidget, &MpvWidget::videoFormatChanged, this, [this] { updateCodecLabel(); });

    connect(m_audioEngine, &AudioEngine::spectrumReady, this,
            [this](const QVector<double> &mags, const QVector<double> &peaks) {
        if (m_spectrumWidget && m_spectrumWidget->isVisible()) {
            m_spectrumWidget->setData(mags, peaks);
            m_spectrumWidget->setTime(m_mpvWidget->timePos(), m_mpvWidget->duration());
        }
    });
    connect(m_audioEngine, &AudioEngine::mediaError, this, &MainWindow::onMediaError);

    connect(m_decoderGroup, &QActionGroup::triggered, this, &MainWindow::onDecoderTriggered);
    connect(m_audioGroup, &QActionGroup::triggered, this, &MainWindow::onAudioGroupTriggered);
    connect(m_subGroup, &QActionGroup::triggered, this, &MainWindow::onSubtitleGroupTriggered);
    connect(m_channelModeGroup, &QActionGroup::triggered,
            this, &MainWindow::onChannelModeTriggered);
    connect(m_aspectGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        setAspect(a->data().toString());
    });
    connect(m_presetGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        applyAudioPreset(a->data().toInt());
    });

    connect(m_spectrumTimer, &QTimer::timeout, this, &MainWindow::onSpectrumTimer);
    connect(m_lyricTimer, &QTimer::timeout, this, &MainWindow::onLyricTimer);
    connect(m_edgeTimer, &QTimer::timeout, this, &MainWindow::onEdgeTimer);
    connect(m_osdTimer, &QTimer::timeout, this, &MainWindow::onOsdTimer);
}

void MainWindow::loadSettings()
{
    m_settings = new QSettings(QStringLiteral("EchoPlayer"), QStringLiteral("EchoPlayer"), this);
    m_hwdec = m_settings->value("hwdec", "auto").toString();
    m_locale = m_settings->value("language", "zh_CN").toString();
    m_speed = qBound(0.25, m_settings->value("speed", 1.0).toDouble(), 4.0);
    m_volumeBeforeMute = qBound(0, m_settings->value("volume", 100).toInt(), 200);
    m_resumeMap = m_settings->value("resume").toMap();
    m_recentFiles = m_settings->value("recent").toStringList();
    m_repeatMode = qBound(0, m_settings->value("repeatMode", 0).toInt(), 2);
    m_shuffle = m_settings->value("shuffle", false).toBool();
    m_trayOnClose = m_settings->value("minimizeToTray", true).toBool();
    m_playlistManual = m_settings->value("playlistManual", false).toBool();

    if (m_shuffleAction) m_shuffleAction->setChecked(m_shuffle);
    if (m_trayOnCloseAction) m_trayOnCloseAction->setChecked(m_trayOnClose);
    if (m_repeatAction)
        m_repeatAction->setText(tr("循环：%1").arg(repeatModeName()));

    const QByteArray geo = m_settings->value("geometry").toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);

    m_ctrlBar->volumeSlider()->setValue(m_volumeBeforeMute);
    for (QAction *a : m_decoderGroup->actions()) {
        if (a->data().toString() == m_hwdec) {
            a->setChecked(true);
            break;
        }
    }
    LanguageManager::instance().switchLanguage(m_locale);
}

void MainWindow::saveSettings()
{
    if (!m_settings)
        return;
    rememberPosition();
    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("hwdec", m_hwdec);
    m_settings->setValue("language", m_locale);
    m_settings->setValue("speed", m_speed);
    m_settings->setValue("volume", m_ctrlBar->volumeSlider()->value());
    m_settings->setValue("resume", m_resumeMap);
    m_settings->setValue("recent", m_recentFiles);
    m_settings->setValue("repeatMode", m_repeatMode);
    m_settings->setValue("shuffle", m_shuffle);
    m_settings->setValue("minimizeToTray", m_trayOnClose);
    m_settings->sync();
}

QString MainWindow::barePath(const QString &p)
{
    const int q = p.indexOf("?start=");
    return q >= 0 ? p.left(q) : p;
}

double MainWindow::cueOffset(const QString &p)
{
    const int q = p.indexOf("?start=");
    return q >= 0 ? p.mid(q + 7).toDouble() : -1.0;
}

bool MainWindow::isSubtitleFile(const QString &p)
{
    const QString ext = QFileInfo(p).suffix().toLower();
    return ext == "srt" || ext == "ass" || ext == "ssa" ||
           ext == "sub" || ext == "vtt" || ext == "smi" || ext == "idx";
}

void MainWindow::openMedia(const QString &path)
{
    if (m_destroyed || path.isEmpty())
        return;

    rememberPosition();
    const QString actualPath = barePath(path);
    const double startOffset = cueOffset(path);

    m_currentFilePath = actualPath;
    m_currentPlayingPath = path;
    m_pendingSeek = startOffset;
    m_currentIsStream = isNetworkUrl(actualPath);
    m_currentArtUrl.clear();
    m_currentTitle = m_currentIsStream ? displayNameFor(actualPath)
                                       : QFileInfo(actualPath).fileName();
    m_previewSeconds = -1.0;
    hideSeekPreview();

    m_audioEngine->setAnalysisPaused(true);
    m_audioEngine->closeAll();

    bool hasVideo = true;
    bool showSpec = false;
    bool hasPoster = false;

    // Network streams are handled entirely by mpv: probing them with FFmpeg
    // here would block the UI thread on connection timeouts.
    if (!m_currentIsStream && !m_audioEngine->openFile(actualPath)) {
        onMediaError(tr("无法打开文件：%1").arg(actualPath));
        return;
    }
    if (!m_currentIsStream) {
        m_audioEngine->loadLyrics(actualPath);
        hasVideo = m_audioEngine->hasVideoStreams();
        showSpec = !hasVideo;
        if (m_audioEngine->hasEmbeddedPoster()) {
            const QImage poster = m_audioEngine->extractEmbeddedPoster();
            if (!poster.isNull()) {
                const QPixmap px = QPixmap::fromImage(poster);
                m_posterLabel->setPixmap(px);
                m_posterLabel->setFixedSize(px.size());
                m_posterLabel->setVisible(true);
                hasPoster = true;
                exportCoverArt(poster);
            }
        }
        m_thumbs->setSource(actualPath);
        // Warm the first bucket so the hover preview appears instantly.
        m_thumbs->request(0.0);
    } else {
        m_thumbs->clear();
    }

    m_spectrumWidget->setVisible(showSpec);
    if (!hasPoster)
        m_posterLabel->setVisible(false);

    m_titleBar->setFileName(m_currentTitle);
    setWindowTitle(m_currentTitle);
    updateCodecLabel();

    if (showSpec) {
        const int ctrlH = m_ctrlBar->sizeHint().height();
        m_spectrumWidget->setBottomReserve(ctrlH);
        m_spectrumWidget->setFixedHeight((100 + ctrlH) / 2);
        m_spectrumWidget->setTitle(QFileInfo(actualPath).completeBaseName());
        m_spectrumWidget->setTime(0, m_audioEngine->duration());
    }

    m_mpvWidget->loadFile(actualPath);
    scanPlaylist(actualPath);
    sendCurrentFile();

    m_recentFiles.removeAll(actualPath);
    m_recentFiles.prepend(actualPath);
    while (m_recentFiles.size() > 20)
        m_recentFiles.removeLast();
    if (m_settings)
        m_settings->setValue("recent", m_recentFiles);

    m_mprisDurationKnown = false;
    publishMprisMetadata();
    m_mpris->setPlaybackStatus(QStringLiteral("Playing"));
    m_mpris->setCanSeek(true);
    setInhibitForPlayback(true);
    syncTrayMenu();
    m_spectrumTimer->start();
    m_lyricTimer->start();
    repositionBars();
}

void MainWindow::onFileLoaded()
{
    if (m_destroyed)
        return;
    m_ctrlBar->setFileLoaded(true);
    m_ctrlBar->setPlaying(true);
    applyPersistedPlayback();
    // The silent PCM decode is only needed for the spectrum (audio-only files).
    // Starting it for videos would waste a whole extra audio decode thread.
    if (!m_currentIsStream && !m_audioEngine->hasVideoStreams()) {
        m_audioEngine->startAnalysis();
        m_audioEngine->setAnalysisPaused(false);
    } else {
        m_audioEngine->setAnalysisPaused(true);
    }
    populateSubMenu();
    populateAudioMenu();
    updateCodecLabel();

    const double dur = m_mpvWidget->duration();
    qDebug() << "[player] loaded" << m_currentFilePath << "duration" << dur
             << "video" << m_mpvWidget->videoFormat()
             << "audio" << m_audioEngine->audioCodecName();
    double target = m_pendingSeek;
    if (target < 0) {
        const double resume = m_resumeMap.value(m_currentFilePath, 0).toDouble();
        if (dur > 30.0 && resume > 5.0 && resume < dur - 10.0) {
            target = resume;
            showOsd(tr("已接着上次看到 %1").arg(formatTime(resume)), 2200);
        }
    }
    if (target >= 0.0) {
        m_mpvWidget->seek(target);
        if (!m_currentIsStream)
            m_audioEngine->seek(target);
        m_mpris->emitSeeked(target);
    }
    m_pendingSeek = -1.0;

    publishMprisMetadata();
    m_mprisDurationKnown = dur > 0.0;
    m_mpris->setCanSeek(dur > 0.0);
    m_mpris->setPlaybackStatus(QStringLiteral("Playing"));
    syncTrayMenu();
    // Stream metadata (duration, codecs) can arrive after file-loaded.
    QTimer::singleShot(800, this, [this] {
        if (!m_destroyed)
            updateCodecLabel();
    });
    m_spectrumTimer->start();
    m_lyricTimer->start();
    repositionBars();
    if (m_posterLabel->isVisible()) m_posterLabel->raise();
    if (m_lyricLabel->isVisible()) m_lyricLabel->raise();
    if (m_osdLabel->isVisible()) m_osdLabel->raise();
    if (m_infoLabel->isVisible()) m_infoLabel->raise();
}

void MainWindow::applyPersistedPlayback()
{
    m_mpvWidget->setHwDec(m_hwdec);
    m_mpvWidget->setSpeed(m_speed);
    m_mpvWidget->setVolume(m_ctrlBar->volumeSlider()->value());
    m_mpvWidget->setMute(m_muted);
    m_mpvWidget->setVideoRotate(m_rotation);
    m_ctrlBar->setMuted(m_muted);
    // Equalizer, subtitle style and video equalizer are re-applied on every
    // file so they survive a track change / mpv property reset.
    if (m_adjustPanel)
        m_adjustPanel->applyAll();
}

void MainWindow::rememberPosition()
{
    if (m_currentFilePath.isEmpty() || !m_mpvWidget)
        return;
    const double p = m_mpvWidget->timePos();
    const double d = m_mpvWidget->duration();
    if (d > 30.0 && p > 5.0 && p < d - 10.0)
        m_resumeMap[m_currentFilePath] = p;
    else
        m_resumeMap.remove(m_currentFilePath);
    if (m_settings)
        m_settings->setValue("resume", m_resumeMap);
}

void MainWindow::onPlaybackFinished()
{
    if (m_destroyed)
        return;
    qDebug() << "[player] EOF -> advancing playlist";
    m_resumeMap.remove(m_currentFilePath);
    if (m_settings)
        m_settings->setValue("resume", m_resumeMap);
    if (m_repeatMode == 2 && !m_currentPlayingPath.isEmpty()) {
        openMedia(m_currentPlayingPath);   // repeat one
        return;
    }
    playRelative(1);
}

void MainWindow::playRelative(int delta)
{
    const int next = nextPlaylistIndex(delta);
    if (next < 0 || next >= m_playlistPaths.size()) {
        m_ctrlBar->setPlaying(false);
        m_mpris->setPlaybackStatus(QStringLiteral("Stopped"));
        setInhibitForPlayback(false);
        syncTrayMenu();
        showOsd(delta > 0 ? tr("播放结束") : tr("已经是第一个"));
        return;
    }
    openMedia(m_playlistPaths.at(next));
}

int MainWindow::nextPlaylistIndex(int delta) const
{
    const int count = m_playlistPaths.size();
    if (count == 0)
        return -1;
    const int current = m_playlistPaths.indexOf(m_currentPlayingPath);

    if (m_shuffle && delta > 0) {
        if (count == 1)
            return m_repeatMode == 0 ? -1 : 0;
        int pick = QRandomGenerator::global()->bounded(count);
        for (int i = 0; i < 12 && pick == current; ++i)
            pick = QRandomGenerator::global()->bounded(count);
        if (pick == current)
            pick = (current + 1) % count;
        return pick;
    }

    if (current < 0)
        return (m_repeatMode != 0 || delta > 0) ? 0 : -1;
    const int next = current + delta;
    if (next >= 0 && next < count)
        return next;
    if (m_repeatMode == 1)
        return delta > 0 ? 0 : count - 1;
    return -1;
}

void MainWindow::onPrevTrack()
{
    playRelative(-1);
}

void MainWindow::onNextTrack()
{
    playRelative(1);
}

void MainWindow::onOpenFile()
{
    if (m_destroyed)
        return;
    const QString file = QFileDialog::getOpenFileName(this, tr("打开媒体文件"), {},
        "Media files (*.mp4 *.avi *.mkv *.mov *.flv *.wmv *.ts *.webm *.mpeg *.mpg *.m4v "
        "*.3gp *.ogv *.vob *.mts *.m2ts *.rm *.rmvb *.asf *.divx *.xvidd "
        "*.mp3 *.aac *.flac *.wav *.ogg *.oga *.m4a *.wma *.opus *.ac3 *.dts *.alac "
        "*.ape *.wv *.aiff *.aif *.au *.amr *.mid *.midi *.mpc *.cue);;All files (*)");
    if (file.isEmpty())
        return;

    if (file.endsWith(".cue", Qt::CaseInsensitive)) {
        QList<CueTrack> tracks;
        if (parseCueFile(file, tracks) && !tracks.isEmpty()) {
            const QString audio = tracks.first().sourceFile;
            if (!QFileInfo::exists(audio)) {
                showOsd(tr("CUE 引用的音频文件不存在"));
                return;
            }
            m_cueTracks = tracks;
            setPlaylistManual(true);
            applyCueTracks();
            savePlaylistState();
            sendPlaylist();
            openMedia(m_playlistPaths.first());
        }
        return;
    }
    openMedia(file);
}

void MainWindow::onAddSubtitle()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("加载外部字幕"), {},
        "Subtitles (*.srt *.ass *.ssa *.sub *.vtt *.smi);;All files (*)");
    if (file.isEmpty())
        return;
    m_mpvWidget->addSubtitle(file);
    showOsd(tr("已加载字幕：%1").arg(QFileInfo(file).fileName()));
}

void MainWindow::onPlayPause()
{
    if (m_destroyed || m_currentFilePath.isEmpty())
        return;
    if (m_mpvWidget->property("pause").toBool())
        m_mpvWidget->play();
    else
        m_mpvWidget->pause();
}

void MainWindow::onStop()
{
    if (m_destroyed)
        return;
    m_mpvWidget->stop();
    m_audioEngine->setAnalysisPaused(true);
    m_ctrlBar->seekSlider()->setValue(0);
    m_ctrlBar->setTimeText("00:00 / 00:00");
    m_ctrlBar->setCodecText("");
    m_ctrlBar->setPlaying(false);
    m_ctrlBar->setFileLoaded(false);
    m_spectrumTimer->stop();
    m_lyricTimer->stop();
    m_lyricLabel->setVisible(false);
    m_posterLabel->setVisible(false);
    m_lastLyricLine.clear();
    m_spectrumWidget->hide();
    m_spectrumWidget->setData(QVector<double>(32, 0.0), QVector<double>(32, 0.0));
    m_mpris->setPlaybackStatus(QStringLiteral("Stopped"));
    setInhibitForPlayback(false);
    syncTrayMenu();
}

void MainWindow::onPausedChanged(bool paused)
{
    m_ctrlBar->setPlaying(!paused);
    m_audioEngine->setAnalysisPaused(paused);
    if (!m_currentFilePath.isEmpty()) {
        m_mpris->setPlaybackStatus(paused ? QStringLiteral("Paused")
                                          : QStringLiteral("Playing"));
        setInhibitForPlayback(!paused);
        syncTrayMenu();
    }
}

void MainWindow::onSliderPressed()
{
    m_sliderDragging = true;
}

void MainWindow::onSliderReleased()
{
    m_sliderDragging = false;
    double dur = m_mpvWidget->duration();
    if (dur <= 0) dur = m_audioEngine->duration();
    const double pos = m_ctrlBar->seekSlider()->value() / 1000.0 * dur;
    m_mpvWidget->seek(pos);
    if (!m_currentIsStream)
        m_audioEngine->seek(pos);
    m_mpris->emitSeeked(pos);
}

void MainWindow::onSliderMoved(int value)
{
    double dur = m_mpvWidget->duration();
    if (dur <= 0) dur = m_audioEngine->duration();
    const double pos = value / 1000.0 * dur;
    m_ctrlBar->setTimeText(formatTime(pos) + " / " + formatTime(dur));
}

void MainWindow::onPositionChanged(double pos)
{
    if (m_destroyed || m_sliderDragging)
        return;
    double dur = m_mpvWidget->duration();
    if (dur <= 0) dur = m_audioEngine->duration();
    m_ctrlBar->setPosition(pos, dur);
    m_ctrlBar->setTimeText(formatTime(pos) + " / " + formatTime(dur));
    m_mpris->setPositionSeconds(pos);
    if (dur > 0.0 && !m_mprisDurationKnown) {
        m_mprisDurationKnown = true;
        publishMprisMetadata();
        m_mpris->setCanSeek(true);
        updateCodecLabel();
    }
}

void MainWindow::onDurationChanged(double dur)
{
    if (m_destroyed)
        return;
    if (dur <= 0) dur = m_audioEngine->duration();
    m_ctrlBar->setTimeText(dur > 0 ? "00:00 / " + formatTime(dur) : "00:00 / 00:00");
    if (dur > 0) {
        m_mprisDurationKnown = true;
        publishMprisMetadata();
        m_mpris->setCanSeek(true);
        updateCodecLabel();
    }
}

void MainWindow::onMediaError(const QString &msg)
{
    if (!m_destroyed && !msg.isEmpty())
        showOsd(tr("错误：%1").arg(msg), 3000);
}

void MainWindow::onVolumeChanged(int value)
{
    if (m_destroyed)
        return;
    m_volumeBeforeMute = value;
    m_mpvWidget->setVolume(value);
    m_ctrlBar->setVolume(value);
    m_mpris->setVolumePercent(value);
    if (m_muted) {
        m_muted = false;
        m_mpvWidget->setMute(false);
        m_ctrlBar->setMuted(false);
    }
}

void MainWindow::onToggleMute()
{
    if (m_destroyed)
        return;
    m_muted = !m_muted;
    m_mpvWidget->setMute(m_muted);
    m_ctrlBar->setMuted(m_muted);
    showOsd(m_muted ? tr("静音") : tr("取消静音"));
}

void MainWindow::adjustVolume(int delta)
{
    const int v = qBound(0, m_ctrlBar->volumeSlider()->value() + delta, 200);
    m_ctrlBar->volumeSlider()->setValue(v);
    showOsd(tr("音量 %1%").arg(v));
}

void MainWindow::onRewind()
{
    seekRelative(-5);
}

void MainWindow::onForward()
{
    seekRelative(5);
}

void MainWindow::seekRelative(double delta)
{
    if (m_currentFilePath.isEmpty())
        return;
    const double p = m_mpvWidget->timePos();
    const double d = std::max(0.0, m_mpvWidget->duration());
    const double np = qBound(0.0, p + delta, d > 0 ? d : p + delta);
    m_mpvWidget->seek(np);
    if (!m_currentIsStream)
        m_audioEngine->seek(np);
    m_mpris->emitSeeked(np);
    showOsd(tr("%1%2 秒 → %3").arg(delta > 0 ? "+" : "")
           .arg(std::abs(delta), 0, 'f', 0).arg(formatTime(np)));
}

void MainWindow::setSpeed(double speed)
{
    m_speed = qBound(0.25, speed, 4.0);
    m_mpvWidget->setSpeed(m_speed);
    m_mpris->setRate(m_speed);
    showOsd(tr("倍速 %1x").arg(QString::number(m_speed, 'f', 2)));
}

void MainWindow::adjustSubDelay(double delta)
{
    if (m_adjustPanel)
        m_adjustPanel->bumpSubDelay(delta);
    showOsd(tr("字幕延迟 %1 秒").arg(
        QString::number(m_adjustPanel ? m_adjustPanel->subDelay() : 0.0, 'f', 1)));
}

void MainWindow::adjustBrightness(int delta)
{
    if (m_adjustPanel)
        m_adjustPanel->bumpBrightness(delta);
    showOsd(tr("亮度 %1").arg(m_adjustPanel ? m_adjustPanel->brightness() : 0));
}

void MainWindow::setAspect(const QString &aspect)
{
    m_mpvWidget->setAspect(aspect);
    for (QAction *a : m_aspectGroup->actions())
        a->setChecked(a->data().toString() == aspect);
    showOsd(tr("画面比例 %1").arg(aspect == "-1" ? tr("原始") : aspect));
}

void MainWindow::rotateVideo()
{
    m_rotation = (m_rotation + 90) % 360;
    m_mpvWidget->setVideoRotate(m_rotation);
    showOsd(tr("旋转 %1°").arg(m_rotation));
}

void MainWindow::resetPicture()
{
    m_rotation = 0;
    if (m_adjustPanel)
        m_adjustPanel->resetVideo();
    m_mpvWidget->setVideoRotate(0);
    setAspect("-1");
    showOsd(tr("画面设置已重置"));
}

void MainWindow::applyAudioPreset(int index)
{
    m_presetIndex = qBound(0, index, 3);
    // AdjustPanel is the single owner of the "af" property: a menu preset is
    // just a band configuration plus the same filter chain builder.
    if (m_adjustPanel)
        m_adjustPanel->applyPreset(m_presetIndex);
    const QList<QAction *> acts = m_presetGroup->actions();
    if (m_presetIndex < acts.size()) {
        acts.at(m_presetIndex)->setChecked(true);
        showOsd(tr("音效: %1").arg(acts.at(m_presetIndex)->text()));
    }
}

void MainWindow::cycleAudioPreset()
{
    applyAudioPreset((m_presetIndex + 1) % 4);
}

void MainWindow::markABLoop()
{
    const double p = m_mpvWidget->timePos();
    if (p < 0) return;
    if (m_abA < 0) {
        m_abA = p;
        m_mpvWidget->setABLoopA(p);
        showOsd(tr("A 点已设：%1，再按 A 设 B 点").arg(formatTime(p)));
    } else {
        m_mpvWidget->setABLoopB(p);
        showOsd(tr("A/B 循环：%1 - %2").arg(formatTime(m_abA), formatTime(p)));
        m_abA = -1.0;
    }
}

void MainWindow::screenshot()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                        + QStringLiteral("/EchoPlayer");
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/echo_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))
        + QStringLiteral(".png");
    m_mpvWidget->screenshot(file);
    showOsd(tr("截图已保存到 Pictures/EchoPlayer"), 2200);
}

void MainWindow::toggleMediaInfo()
{
    m_infoVisible = !m_infoVisible;
    if (!m_infoVisible) {
        m_infoLabel->setVisible(false);
        return;
    }
    auto p = [this](const QString &key) { return m_mpvWidget->property(key).toString(); };
    QStringList lines;
    lines << tr("文件: %1").arg(QFileInfo(m_currentFilePath).fileName());
    lines << tr("时长: %1").arg(formatTime(m_mpvWidget->duration()));
    lines << tr("容器: %1").arg(p("file-format"));
    lines << tr("视频: %1  %2x%3").arg(m_mpvWidget->videoFormat(),
                                       p("width"), p("height"));
    lines << tr("帧率: %1").arg(p("container-fps"));
    lines << tr("音频: %1").arg(m_audioEngine->audioCodecName());
    lines << tr("倍速: %1x").arg(QString::number(m_speed, 'f', 2));
    lines << tr("解码: %1").arg(p("hwdec-current"));
    lines << tr("字幕: %1").arg(p("sid"));
    m_infoLabel->setText(lines.join('\n'));
    m_infoLabel->adjustSize();
    m_infoLabel->setVisible(true);
    repositionBars();
    m_infoLabel->raise();
}

void MainWindow::showOsd(const QString &text, int ms)
{
    if (!m_osdLabel || m_destroyed)
        return;
    m_osdLabel->setText(text);
    m_osdLabel->adjustSize();
    m_osdLabel->setVisible(true);
    repositionBars();
    m_osdLabel->raise();
    m_osdTimer->start(ms);
}

void MainWindow::onOsdTimer()
{
    if (m_osdLabel)
        m_osdLabel->setVisible(false);
}

void MainWindow::updateCodecLabel()
{
    QString v = m_mpvWidget->videoFormat();
    QString a = m_audioEngine->audioCodecName();
    // The analysis engine is skipped for streams, so ask mpv directly.
    if (a.isEmpty()) {
        const QString fromMpv = m_mpvWidget->property("audio-codec").toString();
        if (!fromMpv.isEmpty())
            a = fromMpv;
    }
    if (v.isEmpty()) {
        const QString fromMpv = m_mpvWidget->property("video-format").toString();
        if (!fromMpv.isEmpty())
            v = fromMpv;
    }
    if (v.isEmpty() && a.isEmpty() && m_currentIsStream)
        v = tr("网络流");
    if (!v.isEmpty() && !a.isEmpty())
        v += " / ";
    v += a;
    m_ctrlBar->setCodecText(v);
}

QString MainWindow::formatTime(double seconds) const
{
    if (seconds < 0) seconds = 0;
    const int total = static_cast<int>(std::round(seconds));
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

// ---- Playlist process ----

void MainWindow::launchPlaylistApp()
{
    if (m_playlistProcess)
        return;
    QString appPath = QApplication::applicationDirPath() + "/EchoPlayer-playlist";
    if (!QFileInfo::exists(appPath)) {
        appPath = QApplication::applicationDirPath() + "/../EchoPlayer-playlist";
        if (!QFileInfo::exists(appPath))
            appPath = QApplication::applicationDirPath() + "/../../EchoPlayer-playlist";
    }
    if (!QFileInfo::exists(appPath)) {
        showOsd(tr("未找到播放列表程序"));
        return;
    }

    m_playlistProcess = new QProcess(this);
    m_playlistProcess->setProcessChannelMode(QProcess::SeparateChannels);
    m_playlistProcess->setProgram(appPath);
    connect(m_playlistProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onPlaylistStdoutReady);
    connect(m_playlistProcess, &QProcess::started,
            this, &MainWindow::onPlaylistProcessStarted);
    m_playlistProcess->start();
    m_playlistProcess->setReadChannel(QProcess::StandardOutput);
}

void MainWindow::onPlaylistProcessStarted()
{
    sendPlaylist();
    sendPlaylistMode();
    sendPlaylistGeometry();
    sendPlaylistLanguage();
    if (!m_playlistPaths.isEmpty())
        sendCurrentFile();
}

void MainWindow::onPlaylistStdoutReady()
{
    if (!m_playlistProcess)
        return;
    m_playlistReadBuf.append(m_playlistProcess->readAllStandardOutput());
    while (true) {
        const int nl = m_playlistReadBuf.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_playlistReadBuf.left(nl).trimmed();
        m_playlistReadBuf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        const QString text = QString::fromUtf8(line);
        if (text.startsWith("SELECT\t"))
            onPlaylistFileSelected(text.mid(7));
        else if (text.startsWith("CMD\t"))
            onPlaylistCommand(text.split('\t'));
    }
}

void MainWindow::sendPlaylist()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running)
        return;
    m_playlistProcess->write((PlaylistProtocol::makePlaylist(m_playlistPaths, m_playlistNames)
                              + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::sendPlaylistGeometry()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running)
        return;
    const QRect g = geometry();
    m_playlistProcess->write((PlaylistProtocol::makeGeometry(
        g.x(), g.y(), g.width(), g.height()) + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::sendPlaylistLanguage()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running)
        return;
    m_playlistProcess->write((PlaylistProtocol::makeLanguage(m_locale) + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::sendCurrentFile()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running)
        return;
    if (m_currentPlayingPath.isEmpty())
        return;
    m_playlistProcess->write((PlaylistProtocol::makeCurrent(m_currentPlayingPath)
                              + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::closePlaylist()
{
    if (!m_playlistProcess)
        return;
    m_playlistProcess->kill();
    m_playlistProcess->waitForFinished(500);
    m_playlistProcess->deleteLater();
    m_playlistProcess = nullptr;
    m_playlistReadBuf.clear();
}

void MainWindow::onTogglePlaylist()
{
    if (m_destroyed)
        return;
    if (m_playlistProcess)
        closePlaylist();
    else
        launchPlaylistApp();
}

void MainWindow::onPlaylistFileSelected(const QString &path)
{
    openMedia(path);
}

void MainWindow::scanPlaylist(const QString &filePath)
{
    // A user-curated playlist is never overwritten by directory scanning.
    if (m_playlistManual) {
        if (!m_currentPlayingPath.isEmpty()
            && !m_playlistPaths.contains(m_currentPlayingPath)) {
            m_playlistPaths.prepend(m_currentPlayingPath);
            m_playlistNames.prepend(displayNameFor(m_currentPlayingPath));
            savePlaylistState();
        }
        sendPlaylist();
        return;
    }
    if (isNetworkUrl(filePath)) {
        m_playlistPaths = {filePath};
        m_playlistNames = {displayNameFor(filePath)};
        m_cueTracks.clear();
        sendPlaylist();
        return;
    }

    const QFileInfo fi(filePath);
    const QString dir = fi.absolutePath();
    const QString ext = fi.suffix();

    m_playlistPaths.clear();
    m_playlistNames.clear();
    m_cueTracks.clear();

    const QString cuePath = dir + "/" + fi.completeBaseName() + ".cue";
    if (QFileInfo::exists(cuePath) && parseCueFile(cuePath, m_cueTracks)) {
        applyCueTracks();
    } else {
        QDir d(dir);
        for (const QFileInfo &entry : d.entryInfoList({"*." + ext}, QDir::Files, QDir::Name)) {
            m_playlistPaths << entry.absoluteFilePath();
            m_playlistNames << entry.completeBaseName();
        }
    }
    sendPlaylist();
}

bool MainWindow::parseCueFile(const QString &cuePath, QList<CueTrack> &tracks)
{
    tracks.clear();
    const QString content = readTextAutoDetect(cuePath);
    if (content.isEmpty())
        return false;

    const QString cueDir = QFileInfo(cuePath).absolutePath();
    QString currentFile;
    CueTrack currentTrack;
    bool inTrack = false;
    const QRegularExpression fileRe(R"(^\s*FILE\s+\"(.*?)\"\s+(\S+))");
    const QRegularExpression trackRe(R"(^\s*TRACK\s+(\d+)\s+AUDIO)");
    const QRegularExpression titleRe(R"(^\s*TITLE\s+\"(.*)\")");
    const QRegularExpression performerRe(R"(^\s*PERFORMER\s+\"(.*)\")");
    const QRegularExpression indexRe(R"(^\s*INDEX\s+01\s+(\d+):(\d+):(\d+))");

    for (const QString &line : content.split('\n')) {
        QRegularExpressionMatch m = fileRe.match(line);
        if (m.hasMatch()) {
            currentFile = m.captured(1);
            if (!QDir::isAbsolutePath(currentFile))
                currentFile = QDir(cueDir).absoluteFilePath(currentFile);
        }
        m = trackRe.match(line);
        if (m.hasMatch()) {
            if (inTrack && !currentTrack.title.isEmpty())
                tracks.append(currentTrack);
            currentTrack = CueTrack();
            currentTrack.trackNo = m.captured(1).toInt();
            currentTrack.sourceFile = currentFile;
            inTrack = true;
        }
        m = titleRe.match(line);
        if (m.hasMatch() && inTrack)
            currentTrack.title = m.captured(1);
        m = performerRe.match(line);
        if (m.hasMatch() && inTrack)
            currentTrack.performer = m.captured(1);
        m = indexRe.match(line);
        if (m.hasMatch() && inTrack) {
            currentTrack.startTime = m.captured(1).toInt() * 60.0
                + m.captured(2).toInt() + m.captured(3).toInt() / 75.0;
        }
    }
    if (inTrack && !currentTrack.title.isEmpty())
        tracks.append(currentTrack);
    return !tracks.isEmpty();
}

void MainWindow::applyCueTracks()
{
    m_playlistPaths.clear();
    m_playlistNames.clear();
    for (const CueTrack &ct : m_cueTracks) {
        m_playlistPaths << ct.sourceFile
            + QStringLiteral("?start=%1").arg(ct.startTime, 0, 'f', 3);
        m_playlistNames << QStringLiteral("%1 - %2")
            .arg(ct.trackNo, 2, 10, QChar('0')).arg(ct.title);
    }
}

// ---- Menus ----

void MainWindow::populateSubMenu()
{
    for (QAction *a : m_subGroup->actions())
        m_subGroup->removeAction(a);
    m_subTrackMenu->clear();

    const qlonglong current = m_mpvWidget->property("sid").toLongLong();
    QAction *off = m_subTrackMenu->addAction(tr("关闭"));
    off->setCheckable(true);
    off->setChecked(current <= 0);
    off->setData(qlonglong(-1));
    m_subGroup->addAction(off);

    for (const TrackEntry &t : mpvTracks(m_mpvWidget->handle(), "sub")) {
        QAction *a = m_subTrackMenu->addAction(t.label);
        a->setCheckable(true);
        a->setChecked(t.id == current);
        a->setData(t.id);
        m_subGroup->addAction(a);
    }
}

void MainWindow::populateAudioMenu()
{
    for (QAction *a : m_audioGroup->actions())
        m_audioGroup->removeAction(a);
    m_audioMenu->clear();

    const qlonglong current = m_mpvWidget->property("aid").toLongLong();
    const QList<TrackEntry> tracks = mpvTracks(m_mpvWidget->handle(), "audio");
    for (const TrackEntry &t : tracks) {
        QAction *a = m_audioMenu->addAction(t.label);
        a->setCheckable(true);
        a->setChecked(t.id == current);
        a->setData(t.id);
        m_audioGroup->addAction(a);
    }
    if (tracks.isEmpty())
        m_audioMenu->addAction(tr("无音轨"))->setEnabled(false);
}

void MainWindow::onSubtitleGroupTriggered(QAction *act)
{
    m_mpvWidget->setSubtitleTrack(act->data().toLongLong());
}

void MainWindow::onAudioGroupTriggered(QAction *act)
{
    m_mpvWidget->setAudioTrack(act->data().toLongLong());
}

void MainWindow::onChannelModeTriggered(QAction *act)
{
    const int mode = act->data().toInt();
    m_mpvWidget->setChannelBalance(mode == 0 ? 0.0 : (mode == 1 ? -1.0 : 1.0));
    showOsd(tr("声道: %1").arg(act->text()));
}

void MainWindow::onDecoderTriggered(QAction *act)
{
    if (m_destroyed || m_currentFilePath.isEmpty())
        return;
    m_hwdec = act->data().toString();
    m_mpvWidget->setHwDec(m_hwdec);
    if (m_settings)
        m_settings->setValue("hwdec", m_hwdec);
    showOsd(tr("硬件解码: %1").arg(act->text()));
}

void MainWindow::onLanguageSwitch(const QString &locale)
{
    m_locale = locale;
    LanguageManager::instance().switchLanguage(locale);
    if (m_settings)
        m_settings->setValue("language", locale);
    sendPlaylistLanguage();
}

// ---- Timers ----

void MainWindow::onSpectrumTimer()
{
    if (!m_destroyed && m_audioEngine && m_spectrumWidget && m_spectrumWidget->isVisible())
        m_audioEngine->triggerSpectrumUpdate();
}

void MainWindow::onLyricTimer()
{
    if (m_destroyed || !m_audioEngine)
        return;
    const double pos = m_mpvWidget->timePos();
    const QList<LyricLine> &lyrics = m_audioEngine->lyrics();
    QString line;
    for (int i = lyrics.size() - 1; i >= 0; --i) {
        if (pos >= lyrics[i].timestamp) {
            line = lyrics[i].text;
            break;
        }
    }
    if (line != m_lastLyricLine) {
        m_lastLyricLine = line;
        m_lyricLabel->setText(line);
        m_lyricLabel->adjustSize();
        m_lyricLabel->setVisible(!line.isEmpty());
        if (!line.isEmpty())
            repositionBars();
    }
}

void MainWindow::onEdgeTimer()
{
    if (m_destroyed || !isFullScreen()) {
        m_edgeTimer->stop();
        return;
    }
    const QPoint local = mapFromGlobal(QCursor::pos());
    const int h = height();
    const int edge = 8;

    if (local.y() <= edge) {
        if (!m_titleVisible) showTitleBar();
    } else if (m_titleVisible && local.y() > m_titleBarHeight) {
        hideTitleBar();
    }
    if (local.y() >= h - edge) {
        if (!m_ctrlVisible) {
            m_ctrlBarHeight = m_ctrlBar->sizeHint().height();
            showCtrlBar();
        }
    } else if (m_ctrlVisible && local.y() < h - m_ctrlBarHeight) {
        hideCtrlBar();
    }
}

// ---- Fullscreen overlay ----

void MainWindow::onToggleFullscreen()
{
    if (m_destroyed)
        return;
    if (isFullScreen()) {
        m_edgeTimer->stop();
        showNormal();
        m_titleVisible = true;
        m_ctrlVisible = true;
        repositionBars();
    } else {
        m_titleVisible = false;
        m_ctrlVisible = false;
        showFullScreen();
        m_ctrlBarHeight = m_ctrlBar->sizeHint().height();
        repositionBars();
        m_edgeTimer->start();
    }
    m_mpris->setFullscreen(isFullScreen());
}

void MainWindow::repositionBars()
{
    if (!m_titleBar || !m_ctrlBar || m_destroyed || !centralWidget())
        return;
    m_ctrlBarHeight = m_ctrlBar->sizeHint().height();
    const int vw = width();
    const int vh = centralWidget()->height();

    auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    if (layout)
        layout->setContentsMargins(0, isFullScreen() ? 0 : m_titleBarHeight,
                                   0, isFullScreen() ? 0 : m_ctrlBarHeight);

    m_titleBar->setGeometry(0, m_titleVisible ? 0 : -m_titleBarHeight, vw, m_titleBarHeight);
    m_ctrlBar->setGeometry(0, m_ctrlVisible ? vh - m_ctrlBarHeight : vh,
                           vw, m_ctrlBarHeight);

    const int mw = m_mpvWidget->width();
    const int mh = m_mpvWidget->height();
    const QPoint videoTopLeft = m_mpvWidget->mapTo(centralWidget(), QPoint(0, 0));

    if (m_posterLabel && m_posterLabel->isVisible()) {
        const QPixmap px = m_posterLabel->pixmap();
        if (!px.isNull()) {
            const QSize scaled = px.size().scaled(mw, mh, Qt::KeepAspectRatio);
            m_posterLabel->setFixedSize(scaled);
            m_posterLabel->move(videoTopLeft.x() + (mw - scaled.width()) / 2,
                                videoTopLeft.y() + (mh - scaled.height()) / 2);
        }
    }

    if (m_lyricLabel && m_lyricLabel->isVisible()) {
        const int lw = qMin(vw - 40, 800);
        m_lyricLabel->setFixedWidth(lw);
        m_lyricLabel->adjustSize();
        int lyricY = videoTopLeft.y() + mh - 40 - m_lyricLabel->height();
        if (lyricY < videoTopLeft.y()) lyricY = videoTopLeft.y();
        m_lyricLabel->move(videoTopLeft.x() + (mw - m_lyricLabel->width()) / 2, lyricY);
    }

    if (m_osdLabel && m_osdLabel->isVisible()) {
        m_osdLabel->adjustSize();
        m_osdLabel->move((vw - m_osdLabel->width()) / 2, m_titleBarHeight + 16);
    }

    if (m_infoLabel && m_infoLabel->isVisible()) {
        m_infoLabel->adjustSize();
        m_infoLabel->move(20, m_titleBarHeight + 16);
    }
}

void MainWindow::showTitleBar()
{
    if (m_titleVisible || !isFullScreen()) return;
    m_titleVisible = true;
    m_titleAnim->stop();
    m_titleAnim->setTargetObject(m_titleBar);
    m_titleAnim->setPropertyName("geometry");
    m_titleAnim->setDuration(200);
    m_titleAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_titleAnim->setStartValue(QRect(0, -m_titleBarHeight, width(), m_titleBarHeight));
    m_titleAnim->setEndValue(QRect(0, 0, width(), m_titleBarHeight));
    m_titleAnim->start();
}

void MainWindow::hideTitleBar()
{
    if (!m_titleVisible || !isFullScreen()) return;
    m_titleVisible = false;
    m_titleAnim->stop();
    m_titleAnim->setTargetObject(m_titleBar);
    m_titleAnim->setPropertyName("geometry");
    m_titleAnim->setDuration(200);
    m_titleAnim->setEasingCurve(QEasingCurve::InCubic);
    m_titleAnim->setStartValue(QRect(0, 0, width(), m_titleBarHeight));
    m_titleAnim->setEndValue(QRect(0, -m_titleBarHeight, width(), m_titleBarHeight));
    m_titleAnim->start();
}

void MainWindow::showCtrlBar()
{
    if (m_ctrlVisible || !isFullScreen()) return;
    m_ctrlVisible = true;
    m_ctrlAnim->stop();
    m_ctrlAnim->setTargetObject(m_ctrlBar);
    m_ctrlAnim->setPropertyName("geometry");
    m_ctrlAnim->setDuration(200);
    m_ctrlAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_ctrlAnim->setStartValue(QRect(0, height(), width(), m_ctrlBarHeight));
    m_ctrlAnim->setEndValue(QRect(0, height() - m_ctrlBarHeight, width(), m_ctrlBarHeight));
    m_ctrlAnim->start();
}

void MainWindow::hideCtrlBar()
{
    if (!m_ctrlVisible || !isFullScreen()) return;
    m_ctrlVisible = false;
    m_ctrlAnim->stop();
    m_ctrlAnim->setTargetObject(m_ctrlBar);
    m_ctrlAnim->setPropertyName("geometry");
    m_ctrlAnim->setDuration(200);
    m_ctrlAnim->setEasingCurve(QEasingCurve::InCubic);
    m_ctrlAnim->setStartValue(QRect(0, height() - m_ctrlBarHeight, width(), m_ctrlBarHeight));
    m_ctrlAnim->setEndValue(QRect(0, height(), width(), m_ctrlBarHeight));
    m_ctrlAnim->start();
}

void MainWindow::updateTitleBar()
{
    if (m_titleBar)
        m_titleBar->setFileName(windowTitle().isEmpty() ? "EchoPlayer" : windowTitle());
}

// ---- Events ----

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    repositionBars();
    sendPlaylistGeometry();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    sendPlaylistGeometry();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    repositionBars();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_forceQuit && m_trayOnClose && m_trayIcon && m_trayIcon->isVisible()
        && QSystemTrayIcon::isSystemTrayAvailable()) {
        saveSettings();
        hide();
        event->ignore();
        if (!m_trayNoticeShown) {
            m_trayNoticeShown = true;
            m_trayIcon->showMessage(QStringLiteral("EchoPlayer"),
                tr("已最小化到托盘，右键托盘图标可退出"),
                QSystemTrayIcon::Information, 3000);
        }
        return;
    }
    saveSettings();
    closePlaylist();
    m_forceQuit = true;
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateDynamic();
        syncTrayMenu();
    } else if (event->type() == QEvent::WindowStateChange) {
        if (m_titleBar)
            m_titleBar->updateMaximizeIcon(isMaximized());
        if (!isFullScreen()) {
            m_edgeTimer->stop();
            m_titleVisible = true;
            m_ctrlVisible = true;
            repositionBars();
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData())
        return;
    QStringList media;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString p = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (p.isEmpty())
            continue;
        if (url.isLocalFile() && isSubtitleFile(p)) {
            m_mpvWidget->addSubtitle(p);
            showOsd(tr("已加载字幕：%1").arg(QFileInfo(p).fileName()));
            continue;
        }
        media << p;
    }
    if (media.size() > 1)
        addFilesToPlaylist(media);
    if (!media.isEmpty())
        openMedia(media.first());
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (m_destroyed)
        return false;

    if (obj == m_mpvWidget && event->type() == QEvent::ContextMenu) {
        auto *ce = static_cast<QContextMenuEvent *>(event);
        m_contextMenu->popup(ce->globalPos());
        return true;
    }
    if (obj == m_mpvWidget && event->type() == QEvent::MouseButtonDblClick) {
        onToggleFullscreen();
        return true;
    }

    QSlider *seek = m_ctrlBar ? m_ctrlBar->seekSlider() : nullptr;
    if (obj == seek) {
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            double dur = m_mpvWidget->duration();
            if (dur <= 0) dur = m_audioEngine->duration();
            if (dur > 0) {
                const int x = me->position().toPoint().x();
                const double ratio = qBound(0.0,
                    static_cast<double>(x) / qMax(1, seek->width()), 1.0);
                showSeekPreview(x, ratio * dur);
            }
        } else if (event->type() == QEvent::Leave) {
            hideSeekPreview();
        }
    }

    const auto *prevBtn = m_ctrlBar ? m_ctrlBar->prevBtn() : nullptr;
    const auto *nextBtn = m_ctrlBar ? m_ctrlBar->nextBtn() : nullptr;
    if (obj == prevBtn || obj == nextBtn) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::RightButton) {
                if (obj == prevBtn) onPrevTrack();
                else onNextTrack();
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu)
            return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::activateWithFiles(const QStringList &paths)
{
    showNormal();
    raise();
    activateWindow();
    if (paths.size() > 1)
        addFilesToPlaylist(paths);
    if (!paths.isEmpty())
        openMedia(paths.first());
}

// ---------------------------------------------------------------- URL helpers

bool MainWindow::isNetworkUrl(const QString &p)
{
    static const QStringList schemes = {
        QStringLiteral("http"), QStringLiteral("https"),
        QStringLiteral("rtsp"), QStringLiteral("rtsps"),
        QStringLiteral("rtmp"), QStringLiteral("rtmps"),
        QStringLiteral("srt"), QStringLiteral("udp"), QStringLiteral("tcp"),
        QStringLiteral("ftp"), QStringLiteral("mms"), QStringLiteral("mmsh"),
        QStringLiteral("dav"), QStringLiteral("davs"), QStringLiteral("smb")
    };
    const QUrl url(p);
    return url.isValid() && schemes.contains(url.scheme().toLower());
}

QString MainWindow::displayNameFor(const QString &p)
{
    const QString bare = barePath(p);
    if (isNetworkUrl(bare)) {
        const QUrl url(bare);
        const QString file = url.fileName(QUrl::FullyDecoded);
        return file.isEmpty() ? url.host() : file;
    }
    return QFileInfo(bare).fileName();
}

QString MainWindow::repeatModeName() const
{
    switch (m_repeatMode) {
    case 1: return tr("列表循环");
    case 2: return tr("单曲循环");
    default: return tr("不循环");
    }
}

// -------------------------------------------------------- desktop integration

void MainWindow::setupDesktopIntegration()
{
    m_inhibitor = new ScreenSaverInhibitor(this);
    m_mpris = new MprisController(this);
    m_mpris->setRate(m_speed);
    m_mpris->setVolumePercent(m_ctrlBar->volumeSlider()->value());
    m_mpris->setRepeatMode(m_repeatMode);
    m_mpris->setShuffle(m_shuffle);
    m_mpris->setFullscreen(isFullScreen());

    connect(m_mpris, &MprisController::playRequested, this, [this] {
        if (!m_currentFilePath.isEmpty()) m_mpvWidget->play();
    });
    connect(m_mpris, &MprisController::pauseRequested, this, [this] {
        if (!m_currentFilePath.isEmpty()) m_mpvWidget->pause();
    });
    connect(m_mpris, &MprisController::playPauseRequested,
            this, &MainWindow::onPlayPause);
    connect(m_mpris, &MprisController::stopRequested,
            this, &MainWindow::onStop);
    connect(m_mpris, &MprisController::nextRequested,
            this, &MainWindow::onNextTrack);
    connect(m_mpris, &MprisController::previousRequested,
            this, &MainWindow::onPrevTrack);
    connect(m_mpris, &MprisController::seekRequested, this,
            [this](double delta) { seekRelative(delta); });
    connect(m_mpris, &MprisController::setPositionRequested, this,
            [this](double seconds) {
        m_mpvWidget->seek(seconds);
        if (!m_currentIsStream) m_audioEngine->seek(seconds);
        m_mpris->emitSeeked(seconds);
    });
    connect(m_mpris, &MprisController::openUriRequested, this,
            [this](const QString &uri) { openMedia(uri); });
    connect(m_mpris, &MprisController::raiseRequested, this, [this] {
        showNormal(); raise(); activateWindow();
    });
    connect(m_mpris, &MprisController::quitRequested, this, [this] {
        m_forceQuit = true;
        close();
    });
    connect(m_mpris, &MprisController::volumeChangeRequested, this,
            [this](double v) {
        m_ctrlBar->volumeSlider()->setValue(qBound(0, static_cast<int>(v * 100), 200));
    });
    connect(m_mpris, &MprisController::rateChangeRequested, this,
            [this](double r) { setSpeed(r); });
    connect(m_mpris, &MprisController::repeatModeChangeRequested, this,
            [this](int mode) {
        m_repeatMode = qBound(0, mode, 2);
        if (m_repeatAction)
            m_repeatAction->setText(tr("循环：%1").arg(repeatModeName()));
        m_mpris->setRepeatMode(m_repeatMode);
        sendPlaylistMode();
    });
    connect(m_mpris, &MprisController::shuffleChangeRequested, this,
            [this](bool on) {
        if (m_shuffle != on)
            onToggleShuffle();
    });
    connect(m_mpris, &MprisController::fullscreenChangeRequested, this,
            [this](bool on) {
        if (isFullScreen() != on)
            onToggleFullscreen();
    });

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qDebug() << "[tray] no system tray on this session";
        return;
    }

    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/icon.png")), this);
    m_trayMenu = new QMenu(this);
    connect(m_trayMenu->addAction(tr("显示 / 隐藏")), &QAction::triggered,
            this, [this] {
        if (isVisible()) hide();
        else { showNormal(); raise(); activateWindow(); }
    });
    m_trayMenu->addSeparator();
    m_trayPlayAction = m_trayMenu->addAction(tr("播放 / 暂停"));
    connect(m_trayPlayAction, &QAction::triggered, this, &MainWindow::onPlayPause);
    connect(m_trayMenu->addAction(tr("上一集")), &QAction::triggered,
            this, &MainWindow::onPrevTrack);
    connect(m_trayMenu->addAction(tr("下一集")), &QAction::triggered,
            this, &MainWindow::onNextTrack);
    connect(m_trayMenu->addAction(tr("截图")), &QAction::triggered,
            this, [this] { screenshot(); });
    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction(tr("退出")), &QAction::triggered,
            this, [this] { m_forceQuit = true; close(); });

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip(QStringLiteral("EchoPlayer"));
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) hide();
            else { showNormal(); raise(); activateWindow(); }
        }
    });
    m_trayIcon->show();
    syncTrayMenu();
}

void MainWindow::syncTrayMenu()
{
    if (!m_trayPlayAction)
        return;
    const bool playing = !m_currentFilePath.isEmpty()
        && !m_mpvWidget->property("pause").toBool();
    m_trayPlayAction->setText(playing ? tr("暂停") : tr("播放"));
    if (m_trayIcon)
        m_trayIcon->setToolTip(m_currentTitle.isEmpty()
            ? QStringLiteral("EchoPlayer") : m_currentTitle);
}

void MainWindow::setInhibitForPlayback(bool playing)
{
    if (m_inhibitor)
        m_inhibitor->setInhibited(playing && !m_currentFilePath.isEmpty());
}

void MainWindow::publishMprisMetadata()
{
    if (!m_mpris || m_currentFilePath.isEmpty())
        return;
    double duration = m_mpvWidget->duration();
    if (duration <= 0.0)
        duration = m_audioEngine->duration();
    m_mpris->setSource(m_currentFilePath, m_currentTitle, duration, m_currentArtUrl);
}

void MainWindow::exportCoverArt(const QImage &poster)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return;
    const QByteArray hash = QCryptographicHash::hash(
        m_currentFilePath.toUtf8(), QCryptographicHash::Md5).toHex();
    const QString file = dir + QStringLiteral("/cover-")
        + QString::fromLatin1(hash) + QStringLiteral(".jpg");
    QImage scaled = poster;
    if (scaled.width() > 512)
        scaled = scaled.scaledToWidth(512, Qt::SmoothTransformation);
    if (scaled.save(file, "JPG"))
        m_currentArtUrl = QUrl::fromLocalFile(file).toString();
}

// ------------------------------------------------------------ seek-bar preview

void MainWindow::setupSeekPreview()
{
    m_thumbs = new ThumbnailProvider(this);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(140);
    connect(m_previewTimer, &QTimer::timeout, this, &MainWindow::onPreviewTimer);
    connect(m_thumbs, &ThumbnailProvider::ready,
            this, &MainWindow::onThumbReady);

    m_seekPreview = new QWidget(centralWidget());
    m_seekPreview->setObjectName(QStringLiteral("seekPreview"));
    m_seekPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_seekPreview->setStyleSheet(
        "QWidget#seekPreview { background: rgba(18,18,24,235);"
        " border: 1px solid rgba(255,255,255,60); border-radius: 6px; }"
        "QLabel { color: #eee; background: transparent; }");
    auto *lay = new QVBoxLayout(m_seekPreview);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(3);
    m_seekPreviewImage = new QLabel(m_seekPreview);
    m_seekPreviewImage->setFixedSize(176, 110);
    m_seekPreviewImage->setAlignment(Qt::AlignCenter);
    m_seekPreviewTime = new QLabel(m_seekPreview);
    m_seekPreviewTime->setAlignment(Qt::AlignCenter);
    m_seekPreviewTime->setStyleSheet("font-size: 12px;");
    lay->addWidget(m_seekPreviewImage);
    lay->addWidget(m_seekPreviewTime);
    m_seekPreview->adjustSize();
    m_seekPreview->hide();

    QSlider *slider = m_ctrlBar->seekSlider();
    slider->setMouseTracking(true);
    slider->installEventFilter(this);
}

void MainWindow::showSeekPreview(int sliderX, double seconds)
{
    if (m_destroyed || !m_seekPreview)
        return;
    m_previewX = sliderX;
    m_previewSeconds = seconds;
    m_seekPreviewTime->setText(formatTime(seconds));
    m_seekPreviewImage->setVisible(!m_currentIsStream);
    m_seekPreview->adjustSize();

    QSlider *slider = m_ctrlBar->seekSlider();
    const QPoint global = slider->mapToGlobal(QPoint(sliderX, 0));
    const QPoint local = centralWidget()->mapFromGlobal(global);
    const int w = m_seekPreview->width();
    const int x = qBound(4, local.x() - w / 2,
                         qMax(4, centralWidget()->width() - w - 4));
    const int y = m_ctrlBar->y() - m_seekPreview->height() - 8;
    m_seekPreview->move(x, qMax(4, y));
    m_seekPreview->show();
    m_seekPreview->raise();

    if (!m_currentIsStream && !m_currentFilePath.isEmpty())
        m_previewTimer->start();
    else
        m_previewTimer->stop();
}

void MainWindow::hideSeekPreview()
{
    if (m_previewTimer)
        m_previewTimer->stop();
    m_previewSeconds = -1.0;
    if (m_seekPreview)
        m_seekPreview->hide();
}

void MainWindow::onPreviewTimer()
{
    if (m_previewSeconds >= 0.0 && !m_currentIsStream
        && !m_currentFilePath.isEmpty())
        m_thumbs->request(m_previewSeconds);
}

void MainWindow::onThumbReady(double seconds, const QImage &image)
{
    if (m_destroyed || !m_seekPreview || !m_seekPreview->isVisible()
        || !m_seekPreviewImage->isVisible())
        return;
    if (std::abs(seconds - m_previewSeconds) > ThumbnailProvider::kBucketSeconds)
        return;
    if (image.isNull()) {
        m_seekPreviewImage->clear();
        return;
    }
    m_seekPreviewImage->setPixmap(QPixmap::fromImage(image).scaled(
        m_seekPreviewImage->size(), Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

// ---------------------------------------------------------------- network URL

void MainWindow::onOpenUrl()
{
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, tr("打开网络流"), tr("输入 http / https / rtsp / rtmp 地址："),
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || url.isEmpty())
        return;
    if (!isNetworkUrl(url)) {
        showOsd(tr("请输入带协议头的完整地址，例如 https://..."), 3000);
        return;
    }
    openMedia(url);
}

// -------------------------------------------------------------- playlist logic

void MainWindow::loadPlaylistState()
{
    if (!m_settings || !m_playlistManual)
        return;
    m_playlistPaths = m_settings->value("playlistPaths").toStringList();
    m_playlistNames = m_settings->value("playlistNames").toStringList();
    if (m_playlistNames.size() != m_playlistPaths.size()) {
        m_playlistNames.clear();
        for (const QString &p : m_playlistPaths)
            m_playlistNames << displayNameFor(p);
    }
    if (m_playlistPaths.isEmpty())
        m_playlistManual = false;
}

void MainWindow::savePlaylistState()
{
    if (!m_settings)
        return;
    m_settings->setValue("playlistManual", m_playlistManual);
    m_settings->setValue("playlistPaths", m_playlistPaths);
    m_settings->setValue("playlistNames", m_playlistNames);
}

void MainWindow::setPlaylistManual(bool manual)
{
    if (m_playlistManual == manual)
        return;
    m_playlistManual = manual;
    savePlaylistState();
    qDebug() << "[playlist] mode:" << (manual ? "manual" : "auto");
}

void MainWindow::sendPlaylistMode()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running)
        return;
    m_playlistProcess->write(
        (PlaylistProtocol::makeMode(m_repeatMode, m_shuffle) + "\n").toUtf8());
}

void MainWindow::addFilesToPlaylist(const QStringList &paths)
{
    setPlaylistManual(true);
    int added = 0;
    for (const QString &p : paths) {
        if (p.isEmpty() || m_playlistPaths.contains(p))
            continue;
        m_playlistPaths << p;
        m_playlistNames << displayNameFor(p);
        ++added;
    }
    if (added == 0)
        return;
    savePlaylistState();
    sendPlaylist();
    sendCurrentFile();
    showOsd(tr("已添加 %1 个文件到播放列表").arg(added));
}

void MainWindow::onAddFilesToPlaylist()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("添加文件到播放列表"), {},
        QStringLiteral("Media files (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.ts "
                       "*.mp3 *.flac *.wav *.aac *.ogg *.m4a);;All files (*)"));
    addFilesToPlaylist(files);
}

void MainWindow::removeFromPlaylist(int index)
{
    if (index < 0 || index >= m_playlistPaths.size())
        return;
    setPlaylistManual(true);
    m_playlistPaths.removeAt(index);
    if (index < m_playlistNames.size())
        m_playlistNames.removeAt(index);
    savePlaylistState();
    sendPlaylist();
    sendCurrentFile();
}

void MainWindow::movePlaylistItem(int from, int to)
{
    if (from < 0 || from >= m_playlistPaths.size() || to < 0
        || to >= m_playlistPaths.size() || from == to)
        return;
    setPlaylistManual(true);
    m_playlistPaths.move(from, to);
    if (m_playlistNames.size() == m_playlistPaths.size())
        m_playlistNames.move(from, to);
    savePlaylistState();
    sendPlaylist();
    sendCurrentFile();
}

void MainWindow::onClearPlaylist()
{
    setPlaylistManual(true);
    m_playlistPaths.clear();
    m_playlistNames.clear();
    // Keep the file that is currently playing as the only entry.
    if (!m_currentPlayingPath.isEmpty()) {
        m_playlistPaths << m_currentPlayingPath;
        m_playlistNames << displayNameFor(m_currentPlayingPath);
    }
    savePlaylistState();
    sendPlaylist();
    sendCurrentFile();
    showOsd(tr("播放列表已清空"));
}

void MainWindow::onToggleShuffle()
{
    m_shuffle = !m_shuffle;
    if (m_shuffleAction)
        m_shuffleAction->setChecked(m_shuffle);
    m_mpris->setShuffle(m_shuffle);
    sendPlaylistMode();
    showOsd(m_shuffle ? tr("随机播放：开") : tr("随机播放：关"));
}

void MainWindow::onCycleRepeat()
{
    m_repeatMode = (m_repeatMode + 1) % 3;
    if (m_repeatAction)
        m_repeatAction->setText(tr("循环：%1").arg(repeatModeName()));
    m_mpris->setRepeatMode(m_repeatMode);
    sendPlaylistMode();
    showOsd(tr("循环：%1").arg(repeatModeName()));
}

void MainWindow::onPlaylistCommand(const QStringList &tokens)
{
    if (tokens.size() < 2)
        return;
    const QString &cmd = tokens.at(1);
    const QStringList args = tokens.mid(2);

    if (cmd == QLatin1String("ADD")) {
        addFilesToPlaylist(args);
    } else if (cmd == QLatin1String("REMOVE") && !args.isEmpty()) {
        removeFromPlaylist(args.at(0).toInt());
    } else if (cmd == QLatin1String("MOVE") && args.size() >= 2) {
        movePlaylistItem(args.at(0).toInt(), args.at(1).toInt());
    } else if (cmd == QLatin1String("CLEAR")) {
        onClearPlaylist();
    } else if (cmd == QLatin1String("PLAY") && !args.isEmpty()) {
        openMedia(args.at(0));
    } else if (cmd == QLatin1String("MODE") && args.size() >= 2) {
        m_repeatMode = qBound(0, args.at(0).toInt(), 2);
        m_shuffle = args.at(1).toInt() != 0;
        if (m_repeatAction)
            m_repeatAction->setText(tr("循环：%1").arg(repeatModeName()));
        if (m_shuffleAction)
            m_shuffleAction->setChecked(m_shuffle);
        m_mpris->setRepeatMode(m_repeatMode);
        m_mpris->setShuffle(m_shuffle);
        sendPlaylistMode();
        showOsd(tr("随机 %1 / 循环 %2")
                .arg(m_shuffle ? tr("开") : tr("关"), repeatModeName()));
    }
}
