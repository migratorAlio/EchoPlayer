#include "ui/mainwindow.h"
#include "ui/mpvwidget.h"
#include "ui/titlebar.h"
#include "ui/controlbar.h"
#include "ui/spectrumwidget.h"
#include "core/audioengine.h"
#include "i18n/languagemanager.h"
#include "playlist/playlistprotocol.h"
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QFileDialog>
#include <QStatusBar>
#include <QStringConverter>
#include <QFileInfo>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QEasingCurve>
#include <QTranslator>
#include <QRegularExpression>
#include <QDir>
#include <QLabel>
#include <QFont>
#include <cmath>

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
    m_avSyncTimer = new QTimer(this);
    m_avSyncTimer->setInterval(5000);

    setupUI();
    connectSignals();

    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(50);
    connect(m_edgeTimer, &QTimer::timeout, this, &MainWindow::onEdgeTimer);

    m_lyricLabel = new QLabel(centralWidget());
    m_lyricLabel->setAlignment(Qt::AlignCenter);
    m_lyricLabel->setWordWrap(true);
    m_lyricLabel->setStyleSheet("color: white; font-size: 20px; font-weight: bold;"
                                "background: rgba(0,0,0,120); padding: 6px 16px;"
                                "border-radius: 8px;");
    m_lyricLabel->setVisible(false);

    m_posterLabel = new QLabel(m_mpvWidget);
    m_posterLabel->setAlignment(Qt::AlignCenter);
    m_posterLabel->setStyleSheet("background: transparent;");
    m_posterLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_posterLabel->setVisible(false);

    m_lyricLabel->raise();
}

MainWindow::~MainWindow()
{
    qDebug() << "[MainWindow] destructor begin";
    m_destroyed = true;

    m_audioEngine->stop();
    m_audioEngine->closeAll();

    closePlaylist();
    qDebug() << "[MainWindow] destructor end";
}

void MainWindow::setupUI()
{
    setWindowTitle("EchoPlayer");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    setupMenu();

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
    m_titleBar->raise();

    m_ctrlBar = new ControlBar(central);
    m_ctrlBar->prevBtn()->installEventFilter(this);
    m_ctrlBar->nextBtn()->installEventFilter(this);
    m_ctrlBar->installEventFilter(this);
    m_ctrlBar->raise();

    setCentralWidget(central);
    resize(960, 640);
    central->layout()->activate();
    repositionBars();
}

void MainWindow::setupMenu()
{
    m_contextMenu = new QMenu(this);

    m_subMenu = m_contextMenu->addMenu(tr("字幕"));
    m_subGroup = new QActionGroup(this);
    m_subGroup->setExclusive(true);

    m_videoMenu = m_contextMenu->addMenu(tr("视频"));
    m_decoderMenu = m_videoMenu->addMenu(tr("解码"));
    m_decoderGroup = new QActionGroup(this);
    m_decoderGroup->setExclusive(true);

    auto addDec = [this](const QString &label, const QString &api) {
        QAction *act = m_decoderMenu->addAction(label);
        act->setCheckable(true);
        act->setData(api);
        m_decoderGroup->addAction(act);
        return act;
    };
    addDec(tr("关闭"), "no");
    addDec("Auto", "auto");
    addDec("CUDA", "cuda");
    addDec("VAAPI", "vaapi");
    addDec("VDPAU", "vdpau");
    addDec("Vulkan", "vulkan");
    m_decoderMenu->actions().first()->setChecked(true);

    m_audioTopMenu = m_contextMenu->addMenu(tr("声音"));
    m_audioMenu = m_audioTopMenu->addMenu(tr("选择声音"));
    m_audioGroup = new QActionGroup(this);
    m_audioGroup->setExclusive(true);
    m_channelModeGroup = new QActionGroup(this);
    m_channelModeGroup->setExclusive(true);
}

void MainWindow::populateSubMenu()
{
    // Remove old actions from group BEFORE clear() deletes them
    for (QAction *a : m_subGroup->actions())
        m_subGroup->removeAction(a);
    m_subMenu->clear();

    // Add "Disable subtitles" option
    QAction *offAct = m_subMenu->addAction(tr("关闭"));
    offAct->setCheckable(true);
    offAct->setChecked(true);
    offAct->setData(-1);
    m_subGroup->addAction(offAct);

    // Query track-list from mpv
    mpv_node node;
    if (mpv_get_property(m_mpvWidget->handle(), "track-list", MPV_FORMAT_NODE, &node) < 0)
        return;

    int64_t currentSub = -1;
    mpv_get_property(m_mpvWidget->handle(), "sid", MPV_FORMAT_INT64, &currentSub);

    auto readSubTracks = [&](const mpv_node &n) {
        if (n.format != MPV_FORMAT_NODE_ARRAY) return;
        for (int i = 0; i < n.u.list->num; ++i) {
            const mpv_node &item = n.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;
            bool isSub = false;
            QString lang, title;
            int64_t id = -1;
            for (int j = 0; j < item.u.list->num; ++j) {
                const char *key = item.u.list->keys[j];
                const mpv_node &val = item.u.list->values[j];
                if (strcmp(key, "type") == 0 && val.format == MPV_FORMAT_STRING)
                    isSub = (strcmp(val.u.string, "sub") == 0);
                else if (strcmp(key, "lang") == 0 && val.format == MPV_FORMAT_STRING)
                    lang = QString::fromUtf8(val.u.string);
                else if (strcmp(key, "title") == 0 && val.format == MPV_FORMAT_STRING)
                    title = QString::fromUtf8(val.u.string);
                else if (strcmp(key, "id") == 0 && val.format == MPV_FORMAT_INT64)
                    id = val.u.int64;
            }
            if (isSub && id >= 0) {
                QString label = title.isEmpty() ? (lang.isEmpty() ? QString(tr("字幕 %1")).arg(id) : lang) : title;
                QAction *act = m_subMenu->addAction(label);
                act->setCheckable(true);
                act->setChecked(id == currentSub);
                act->setData(static_cast<qlonglong>(id));
                m_subGroup->addAction(act);
            }
        }
    };
    readSubTracks(node);
    mpv_free_node_contents(&node);
}

void MainWindow::connectSignals()
{
    // TitleBar signals
    connect(m_titleBar, &TitleBar::openFileRequested, this, &MainWindow::onOpenFile);
    connect(m_titleBar, &TitleBar::fullscreenRequested, this, &MainWindow::onToggleFullscreen);

    // ControlBar signals
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

    // MpvWidget signals
    connect(m_mpvWidget, &MpvWidget::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_mpvWidget, &MpvWidget::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_mpvWidget, &MpvWidget::pausedChanged, this, [this](bool p) {
        m_ctrlBar->setPlaying(!p);
        if (p) m_audioEngine->pause();
        else m_audioEngine->play();
    });
    connect(m_audioEngine, &AudioEngine::finished, this, &MainWindow::onFinished);
    connect(m_mpvWidget, &MpvWidget::mediaError, this, &MainWindow::onMediaError);
    connect(m_mpvWidget, &MpvWidget::tracksChanged, this, [this]() {
        populateSubMenu();
    });
    connect(m_mpvWidget, &MpvWidget::fileLoaded, this, [this]() {
        m_ctrlBar->setFileLoaded(true);
        m_ctrlBar->setPlaying(true);
        // Start audio playback
        m_audioEngine->play();
        // Start timers
        m_spectrumTimer->start();
        m_lyricTimer->start();
        if (m_audioEngine->hasVideoStreams())
            m_avSyncTimer->start();
        repositionBars();
        if (m_posterLabel && m_posterLabel->isVisible())
            m_posterLabel->raise();
        if (m_lyricLabel && m_lyricLabel->isVisible())
            m_lyricLabel->raise();
    });

    // AudioEngine signals
    connect(m_audioEngine, &AudioEngine::stateChanged, this, &MainWindow::onAudioStateChanged);
    connect(m_audioEngine, &AudioEngine::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_audioEngine, &AudioEngine::audioTracksChanged, this, [this]() {
        // Audio tracks changed - could update menu if needed
    });
    connect(m_audioEngine, &AudioEngine::spectrumReady, this, [this](QVector<double> mags, QVector<double> peaks) {
        if (m_spectrumWidget && m_spectrumWidget->isVisible()) {
            m_spectrumWidget->setData(mags, peaks);
            m_spectrumWidget->setTime(m_audioEngine->position(), m_audioEngine->duration());
        }
    });

    // Update codec label when mpv reports video format
    connect(m_mpvWidget, &MpvWidget::videoFormatChanged, this, [this](const QString &fmt) {
        if (m_destroyed || m_currentFilePath.isEmpty()) return;
        QString codec = fmt;
        if (!m_audioEngine->audioCodecName().isEmpty()) {
            if (!codec.isEmpty()) codec += " / ";
            codec += m_audioEngine->audioCodecName();
        }
        m_ctrlBar->setCodecText(codec);
    });

    // Menu signals
    connect(m_decoderGroup, &QActionGroup::triggered, this, [this](QAction *act) {
        if (m_destroyed || m_currentFilePath.isEmpty()) return;
        QString api = act->data().toString();
        m_mpvWidget->setHwDec(api);
        openMedia(m_currentFilePath);  // reload with new hwdec
    });
    connect(m_audioGroup, &QActionGroup::triggered, this, &MainWindow::onAudioGroupTriggered);
    connect(m_subGroup, &QActionGroup::triggered, this, [this](QAction *act) {
        if (m_destroyed) return;
        int64_t id = act->data().toLongLong();
        mpv_set_property(m_mpvWidget->handle(), "sid", MPV_FORMAT_INT64, &id);
    });
    connect(m_channelModeGroup, &QActionGroup::triggered, this, &MainWindow::onChannelModeTriggered);
    connect(m_titleBar, &TitleBar::languageSwitchRequested, this, &MainWindow::onLanguageSwitch);

    // Spectrum timer
    connect(m_spectrumTimer, &QTimer::timeout, this, &MainWindow::onSpectrumTimer);

    // Lyric timer
    connect(m_lyricTimer, &QTimer::timeout, this, &MainWindow::onLyricTimer);

    // AV sync timer
    connect(m_avSyncTimer, &QTimer::timeout, this, &MainWindow::onAvSyncTimer);
}

void MainWindow::openMedia(const QString &path)
{
    // Parse optional cue start offset
    QString actualPath = path;
    double startOffset = -1;
    int qpos = path.indexOf("?start=");
    if (qpos >= 0) {
        actualPath = path.left(qpos);
        startOffset = path.mid(qpos + 7).toDouble();
    }

    m_currentFilePath = actualPath;

    // Load in both mpv and audio engine
    m_audioEngine->stop();
    m_audioEngine->closeAll();
    m_mpvWidget->stop();

    if (!m_audioEngine->openFile(actualPath)) {
        emit m_audioEngine->mediaError("Cannot open audio");
        return;
    }

    // Load lyrics from audio file
    m_audioEngine->loadLyrics(actualPath);

    // Determine if this is audio-only (no real video streams)
    bool hasRealVideo = m_audioEngine->hasVideoStreams();
    bool showSpec = !hasRealVideo && m_audioEngine->duration() > 0;

    // Only load in mpv for files with actual video content
    if (hasRealVideo) {
        m_mpvWidget->loadFile(actualPath);
    } else {
        // For audio-only files, stop mpv to prevent it creating a child window
        // that would obscure the poster/spectrum overlay
        m_mpvWidget->stop();
        // Keep mpv state clean for possible later video files
        m_ctrlBar->setFileLoaded(false);
    }

    QFileInfo fi(actualPath);
    m_titleBar->setFileName(fi.fileName());
    setWindowTitle(fi.fileName());
    m_currentPlayingPath = path;
    scanPlaylist(actualPath);
    sendCurrentFile();

    // Codec label
    QString vfmt = m_mpvWidget->videoFormat();
    QString codec;
    if (!vfmt.isEmpty())
        codec = vfmt;
    if (hasRealVideo && !m_audioEngine->audioCodecName().isEmpty()) {
        if (!codec.isEmpty()) codec += " / ";
        codec += m_audioEngine->audioCodecName();
    } else if (!hasRealVideo) {
        codec = m_audioEngine->audioCodecName();
    }
    m_ctrlBar->setCodecText(codec);

    // Spectrum and poster: show spectrum for audio-only files (excludes attached pictures)
    m_spectrumWidget->setVisible(showSpec);

    // Poster: extract and display embedded cover art (regardless of showSpec)
    bool hasPoster = m_audioEngine->hasEmbeddedPoster();
    if (hasPoster) {
        QImage poster = m_audioEngine->extractEmbeddedPoster();
        if (!poster.isNull()) {
            QPixmap px = QPixmap::fromImage(poster);
            m_posterLabel->setPixmap(px);
            m_posterLabel->setFixedSize(px.size());
            m_posterLabel->setVisible(true);
        }
    } else {
        m_posterLabel->setVisible(false);
    }

    if (showSpec) {
        int ctrlH = m_ctrlBar->sizeHint().height();
        m_spectrumWidget->setBottomReserve(ctrlH);
        m_spectrumWidget->setFixedHeight((100 + ctrlH) / 2);
        m_spectrumWidget->setTitle(fi.completeBaseName());
        m_spectrumWidget->setTime(0, m_audioEngine->duration());
    }

    // Audio menu
    // Remove old actions from groups BEFORE clear() deletes them
    for (QAction *a : m_audioGroup->actions()) m_audioGroup->removeAction(a);
    for (QAction *a : m_channelModeGroup->actions()) m_channelModeGroup->removeAction(a);
    m_audioMenu->clear();
    auto addCh = [this](const QString &label, int mode) {
        QAction *a = m_audioMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_audioEngine->channelMode() == mode);
        a->setData(mode);
        m_channelModeGroup->addAction(a);
    };
    addCh(tr("立体声"), 0);
    addCh(tr("左声道"), 1);
    addCh(tr("右声道"), 2);
    m_audioMenu->addSeparator();
    for (const auto &info : m_audioEngine->availableAudioTracks()) {
        QAction *a = m_audioMenu->addAction(info.displayName);
        a->setCheckable(true);
        a->setChecked(m_audioEngine->currentAudioTrack() == info.streamIndex);
        a->setData(info.streamIndex);
        m_audioGroup->addAction(a);
    }

    // Seek to start offset
    if (startOffset >= 0) {
        m_audioEngine->seek(startOffset);
        if (hasRealVideo)
            m_mpvWidget->seek(startOffset);
    }

    // Start audio for audio-only files immediately (no mpv fileLoaded needed)
    if (showSpec) {
        m_audioEngine->play();
        m_spectrumTimer->start();
        m_lyricTimer->start();
    }

    // Reposition overlays for proper sizing of poster and lyrics
    repositionBars();
    if (m_lyricLabel && m_lyricLabel->isVisible())
        m_lyricLabel->raise();
    if (m_posterLabel && m_posterLabel->isVisible())
        m_posterLabel->raise();

    // For video files, audio playback is triggered by mpv's fileLoaded signal;
    // but we still start timers here so spectrum is ready immediately
    if (hasRealVideo) {
        m_spectrumTimer->start();
        m_lyricTimer->start();
    }
}

// ---- Playlist ----

void MainWindow::launchPlaylistApp()
{
    if (m_playlistProcess) return;

    QString appPath = QApplication::applicationDirPath() + "/EchoPlayer-playlist";
    if (!QFileInfo::exists(appPath)) {
        appPath = QApplication::applicationDirPath() + "/../EchoPlayer-playlist";
        if (!QFileInfo::exists(appPath)) {
            appPath = QApplication::applicationDirPath() + "/../../EchoPlayer-playlist";
        }
    }
    if (!QFileInfo::exists(appPath)) {
        qDebug() << "[MainWindow] playlist app not found";
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
    sendPlaylistGeometry();
    if (!m_playlistPaths.isEmpty())
        sendCurrentFile();
}

void MainWindow::onPlaylistStdoutReady()
{
    if (!m_playlistProcess) return;

    m_playlistReadBuf.append(m_playlistProcess->readAllStandardOutput());

    while (true) {
        int nl = m_playlistReadBuf.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_playlistReadBuf.left(nl).trimmed();
        m_playlistReadBuf.remove(0, nl + 1);

        if (line.isEmpty()) continue;
        QString text = QString::fromUtf8(line);

        if (text.startsWith("SELECT\t")) {
            QString path = text.mid(QString("SELECT\t").length());
            if (!path.isEmpty())
                onPlaylistFileSelected(path);
        }
    }
}

void MainWindow::sendPlaylist()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running) return;

    QString msg = PlaylistProtocol::makePlaylist(m_playlistPaths, m_playlistNames);
    m_playlistProcess->write((msg + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::sendPlaylistGeometry()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running) return;

    QRect g = geometry();
    QString msg = PlaylistProtocol::makeGeometry(g.x(), g.y(), g.width(), g.height());
    m_playlistProcess->write((msg + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::sendCurrentFile()
{
    if (!m_playlistProcess || m_playlistProcess->state() != QProcess::Running) return;
    if (m_currentPlayingPath.isEmpty()) return;

    QString msg = PlaylistProtocol::makeCurrent(m_currentPlayingPath);
    m_playlistProcess->write((msg + "\n").toUtf8());
    m_playlistProcess->waitForBytesWritten(500);
}

void MainWindow::closePlaylist()
{
    if (!m_playlistProcess) return;

    m_playlistProcess->kill();
    m_playlistProcess->waitForFinished(500);
    m_playlistProcess->deleteLater();
    m_playlistProcess = nullptr;
    m_playlistReadBuf.clear();
}

// ---- File open / CUE ----

void MainWindow::onOpenFile()
{
    if (m_destroyed) return;
    QString file = QFileDialog::getOpenFileName(this, tr("Open media file"), {},
        "Media files (*.mp4 *.avi *.mkv *.mov *.flv *.wmv *.ts *.webm *.mpeg *.mpg *.m4v "
        "*.3gp *.ogv *.vob *.mts *.m2ts *.rm *.rmvb *.asf *.divx *.xvidd "
        "*.mp3 *.aac *.flac *.wav *.ogg *.oga *.m4a *.wma *.opus *.ac3 *.dts *.alac "
        "*.ape *.wv *.aiff *.aif *.au *.amr *.mid *.midi *.mpc *.cue);;All files (*)");
    if (file.isEmpty()) return;

    // Handle .cue files directly
    if (file.endsWith(".cue", Qt::CaseInsensitive)) {
        QList<CueTrack> cueTracks;
        if (parseCueFile(file, cueTracks) && !cueTracks.isEmpty()) {
            QString audioFile = cueTracks.first().sourceFile;
            if (!QFileInfo::exists(audioFile)) {
                statusBar()->showMessage("Referenced audio file not found: " + audioFile, 5000);
                return;
            }
            m_cueTracks = cueTracks;
            QString firstTrackName = QString("%1 - %2")
                .arg(cueTracks.first().trackNo, 2, 10, QChar('0'))
                .arg(cueTracks.first().title);
            m_currentPlayingPath = audioFile + QString("?start=%1").arg(cueTracks.first().startTime, 0, 'f', 3);
            applyCueTracks();
            sendPlaylist();
            sendCurrentFile();
            openMedia(m_currentPlayingPath);
        }
        return;
    }

    openMedia(file);
}

void MainWindow::onTogglePlaylist()
{
    if (m_destroyed) return;

    if (m_playlistProcess) {
        closePlaylist();
    } else {
        launchPlaylistApp();
    }
}

void MainWindow::onPlaylistFileSelected(const QString &path)
{
    if (m_destroyed) return;
    openMedia(path);
}

static QString readTextFileAutoDetect(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return {};

    QByteArray raw = f.readAll();

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

bool MainWindow::parseCueFile(const QString &cuePath, QList<CueTrack> &tracks)
{
    tracks.clear();
    QString content = readTextFileAutoDetect(cuePath);
    if (content.isEmpty()) return false;

    QString cueDir = QFileInfo(cuePath).absolutePath();
    QString currentFile;
    CueTrack currentTrack;
    bool inTrack = false;
    QRegularExpression fileRe(R"(^\s*FILE\s+\"(.*?)\"\s+(\S+))");
    QRegularExpression trackRe(R"(^\s*TRACK\s+(\d+)\s+AUDIO)");
    QRegularExpression titleRe(R"(^\s*TITLE\s+\"(.*)\")");
    QRegularExpression performerRe(R"(^\s*PERFORMER\s+\"(.*)\")");
    QRegularExpression indexRe(R"(^\s*INDEX\s+01\s+(\d+):(\d+):(\d+))");

    for (const QString &line : content.split('\n')) {
        QRegularExpressionMatch fm = fileRe.match(line);
        if (fm.hasMatch()) {
            currentFile = fm.captured(1);
            if (!QDir::isAbsolutePath(currentFile))
                currentFile = QDir(cueDir).absoluteFilePath(currentFile);
        }

        QRegularExpressionMatch tm = trackRe.match(line);
        if (tm.hasMatch()) {
            if (inTrack && !currentTrack.title.isEmpty())
                tracks.append(currentTrack);
            currentTrack = CueTrack();
            currentTrack.trackNo = tm.captured(1).toInt();
            currentTrack.sourceFile = currentFile;
            inTrack = true;
        }

        QRegularExpressionMatch tlm = titleRe.match(line);
        if (tlm.hasMatch()) {
            if (inTrack)
                currentTrack.title = tlm.captured(1);
        }

        QRegularExpressionMatch pm = performerRe.match(line);
        if (pm.hasMatch()) {
            if (inTrack)
                currentTrack.performer = pm.captured(1);
        }

        QRegularExpressionMatch im = indexRe.match(line);
        if (im.hasMatch()) {
            if (inTrack) {
                int m = im.captured(1).toInt();
                int s = im.captured(2).toInt();
                int f2 = im.captured(3).toInt();
                currentTrack.startTime = m * 60.0 + s + f2 / 75.0;
            }
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

    for (const auto &ct : m_cueTracks) {
        QString pathWithOffset = ct.sourceFile + QString("?start=%1").arg(ct.startTime, 0, 'f', 3);
        m_playlistPaths << pathWithOffset;
        QString name = QString("%1 - %2").arg(ct.trackNo, 2, 10, QChar('0')).arg(ct.title);
        m_playlistNames << name;
    }
}

void MainWindow::scanPlaylist(const QString &filePath)
{
    QFileInfo fi(filePath);
    QString dir = fi.absolutePath();
    QString ext = fi.suffix();

    m_playlistPaths.clear();
    m_playlistNames.clear();
    m_cueTracks.clear();

    QString cuePath = dir + "/" + fi.completeBaseName() + ".cue";
    if (QFileInfo::exists(cuePath) && parseCueFile(cuePath, m_cueTracks)) {
        applyCueTracks();
    } else {
        QDir d(dir);
        for (const auto &entry : d.entryInfoList({"*." + ext}, QDir::Files, QDir::Name)) {
            m_playlistPaths << entry.absoluteFilePath();
            m_playlistNames << entry.completeBaseName();
        }
    }

    sendPlaylist();
}

// ---- Playback controls ----

void MainWindow::onPlayPause()
{
    if (m_destroyed || m_currentFilePath.isEmpty()) return;
    int64_t paused = 1;
    mpv_get_property(m_mpvWidget->handle(), "pause", MPV_FORMAT_FLAG, &paused);
    if (paused) {
        m_mpvWidget->play();
        m_audioEngine->play();
    } else {
        m_mpvWidget->pause();
        m_audioEngine->pause();
    }
}

void MainWindow::onStop()
{
    if (m_destroyed) return;
    m_mpvWidget->stop();
    m_audioEngine->stop();
    m_ctrlBar->seekSlider()->setValue(0);
    m_ctrlBar->setTimeText("00:00 / 00:00");
    m_ctrlBar->setCodecText("");
    m_ctrlBar->setPlaying(false);
    m_ctrlBar->setFileLoaded(false);
    m_spectrumTimer->stop();
    m_lyricTimer->stop();
    m_avSyncTimer->stop();
    m_lyricLabel->setVisible(false);
    m_posterLabel->setVisible(false);
    m_lastLyricLine.clear();
    m_spectrumWidget->hide();
    {
        QVector<double> zeros(32, 0.0);
        m_spectrumWidget->setData(zeros, zeros);
    }
}

void MainWindow::onSliderPressed()
{
    if (m_destroyed) return;
    m_sliderDragging = true;
}

void MainWindow::onSliderReleased()
{
    if (m_destroyed) return;
    m_sliderDragging = false;
    double dur = m_mpvWidget->duration();
    if (dur <= 0) dur = m_audioEngine->duration();
    double pos = m_ctrlBar->seekSlider()->value() / 1000.0 * dur;
    m_audioEngine->seek(pos);
    m_mpvWidget->seek(pos);
}

void MainWindow::onSliderMoved(int position)
{
    if (m_destroyed) return;
    double dur = m_mpvWidget->duration();
    if (dur <= 0) dur = m_audioEngine->duration();
    double pos = position / 1000.0 * dur;
    m_ctrlBar->setTimeText(formatTime(pos) + " / " + formatTime(dur));
}

void MainWindow::onPositionChanged(double pos)
{
    if (m_destroyed) return;
    if (!m_sliderDragging) {
        double dur = m_mpvWidget->duration();
        if (dur <= 0) dur = m_audioEngine->duration();
        m_ctrlBar->setPosition(pos, dur);
        m_ctrlBar->setTimeText(formatTime(pos) + " / " + formatTime(dur));
    }
}

void MainWindow::onDurationChanged(double dur)
{
    if (m_destroyed) return;
    if (dur <= 0) dur = m_audioEngine->duration();
    if (dur > 0)
        m_ctrlBar->setTimeText("00:00 / " + formatTime(dur));
    else
        m_ctrlBar->setTimeText("00:00 / 00:00");
}

void MainWindow::onAudioStateChanged(bool playing)
{
    if (m_destroyed) return;
    m_ctrlBar->setFileLoaded(true);
    m_ctrlBar->setPlaying(playing);
}

void MainWindow::onStateChanged(bool /*playing*/)
{
    // No longer used directly — mpv handles state
}

void MainWindow::onMediaError(const QString &msg)
{
    if (m_destroyed) return;
    QMessageBox::warning(this, tr("Media error"), msg);
}

void MainWindow::onFinished()
{
    if (m_destroyed) return;
    m_ctrlBar->setPlaying(false);
    m_spectrumTimer->stop();
    m_lyricTimer->stop();
    m_avSyncTimer->stop();
    m_lyricLabel->setVisible(false);
    m_posterLabel->setVisible(false);
}

void MainWindow::onLyricTimer()
{
    if (m_destroyed || !m_audioEngine) return;
    double pos = m_audioEngine->position();
    const auto &lyrics = m_audioEngine->lyrics();
    QString line;
    if (!lyrics.isEmpty()) {
        int idx = -1;
        for (int i = lyrics.size() - 1; i >= 0; --i) {
            if (pos >= lyrics[i].timestamp) { idx = i; break; }
        }
        if (idx >= 0)
            line = lyrics[idx].text;
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

void MainWindow::onAvSyncTimer()
{
    if (m_destroyed || m_sliderDragging) return;
    double mpvPos = m_mpvWidget->timePos();
    double aePos = m_audioEngine->position();
    if (mpvPos < 0 || aePos < 0) return;
    double drift = mpvPos - aePos;
    if (std::abs(drift) > 0.5) {
        qDebug() << "[AVSync] drift =" << drift << "s, correcting AudioEngine";
        m_audioEngine->seek(mpvPos);
    }
}

void MainWindow::onEdgeTimer()
{
    if (m_destroyed) { m_edgeTimer->stop(); return; }
    if (!isFullScreen()) { m_edgeTimer->stop(); return; }

    QPoint cursor = QCursor::pos();
    QPoint local = mapFromGlobal(cursor);
    int h = height();
    const int edgeThreshold = 8;

    if (local.y() <= edgeThreshold) {
        if (!m_titleVisible) showTitleBar();
    } else if (m_titleVisible && local.y() > m_titleBarHeight) {
        hideTitleBar();
    }

    if (local.y() >= h - edgeThreshold) {
        if (!m_ctrlVisible) {
            m_ctrlBarHeight = m_ctrlBar->sizeHint().height();
            showCtrlBar();
        }
    } else if (m_ctrlVisible && local.y() < h - m_ctrlBarHeight) {
        hideCtrlBar();
    }
}

void MainWindow::onVolumeChanged(int value)
{
    if (m_destroyed) return;
    m_volumeBeforeMute = value;
    float vol = value / 100.0f;
    qDebug() << "[Volume] slider =" << value << "→ vol =" << vol;
    m_audioEngine->setVolume(vol);
    m_ctrlBar->setVolume(value);
    if (m_muted) {
        m_muted = false;
        m_ctrlBar->setMuted(false);
    }
}

void MainWindow::onLanguageSwitch(const QString &locale)
{
    LanguageManager::instance().switchLanguage(locale);
}

void MainWindow::onToggleMute()
{
    if (m_destroyed) return;
    m_muted = !m_muted;
    if (m_muted) {
        m_volumeBeforeMute = m_ctrlBar->volumeSlider()->value();
        m_ctrlBar->setVolume(0);
        m_ctrlBar->setMuted(true);
        m_audioEngine->setVolume(0.0f);
    } else {
        m_ctrlBar->setVolume(m_volumeBeforeMute);
        m_ctrlBar->setMuted(false);
        m_audioEngine->setVolume(m_volumeBeforeMute / 100.0f);
    }
}

void MainWindow::onToggleFullscreen()
{
    if (m_destroyed) return;
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
}

void MainWindow::repositionBars()
{
    if (!m_titleBar || !m_ctrlBar || m_destroyed) return;
    m_ctrlBarHeight = m_ctrlBar->sizeHint().height();
    int vw = isFullScreen() ? width() : (centralWidget() ? centralWidget()->width() : width());
    int vh = isFullScreen() ? height() : (centralWidget() ? centralWidget()->height() : height());

    auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    if (layout) {
        layout->setContentsMargins(0, isFullScreen() ? 0 : m_titleBarHeight, 0, isFullScreen() ? 0 : m_ctrlBarHeight);
    }

    m_titleBar->setGeometry(0, m_titleVisible ? 0 : -m_titleBarHeight, vw, m_titleBarHeight);
    m_ctrlBar->setGeometry(0, m_ctrlVisible ? vh - m_ctrlBarHeight : vh, vw, m_ctrlBarHeight);

    int mw = m_mpvWidget->width();
    int mh = m_mpvWidget->height();

    if (m_posterLabel && m_posterLabel->isVisible()) {
        QPixmap px = m_posterLabel->pixmap();
        if (!px.isNull()) {
            QSize scaled = px.size().scaled(mw, mh, Qt::KeepAspectRatio);
            m_posterLabel->setFixedSize(scaled);
            m_posterLabel->move((mw - scaled.width()) / 2,
                                (mh - scaled.height()) / 2);
        }
    }

    if (m_lyricLabel && m_lyricLabel->isVisible()) {
        int lw = qMin(vw - 40, 800);
        m_lyricLabel->setFixedWidth(lw);
        m_lyricLabel->adjustSize();
        QPoint lyrTopLeft = m_mpvWidget->mapTo(centralWidget(), QPoint(0, 0));
        int lyricY = lyrTopLeft.y() + mh - 40 - m_lyricLabel->height();
        if (lyricY < lyrTopLeft.y())
            lyricY = lyrTopLeft.y();
        m_lyricLabel->move(lyrTopLeft.x() + (mw - m_lyricLabel->width()) / 2,
                           lyricY);
    }
}

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

void MainWindow::showTitleBar()
{
    if (m_titleVisible || m_destroyed || !isFullScreen()) return;
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
    if (!m_titleVisible || m_destroyed || !isFullScreen()) return;
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
    if (m_ctrlVisible || m_destroyed || !isFullScreen()) return;
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
    if (!m_ctrlVisible || m_destroyed || !isFullScreen()) return;
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

void MainWindow::onRewind()
{
    if (m_destroyed || m_currentFilePath.isEmpty()) return;
    double p = m_mpvWidget->timePos();
    if (p < 0) p = m_audioEngine->position();
    double newPos = std::max(0.0, p - 5.0);
    m_audioEngine->seek(newPos);
    m_mpvWidget->seek(newPos);
}

void MainWindow::onForward()
{
    if (m_destroyed || m_currentFilePath.isEmpty()) return;
    double p = m_mpvWidget->timePos();
    if (p < 0) p = m_audioEngine->position();
    double newPos = std::min(m_mpvWidget->duration(), p + 5.0);
    m_audioEngine->seek(newPos);
    m_mpvWidget->seek(newPos);
}

void MainWindow::onPrevTrack()
{
    if (m_destroyed || m_currentFilePath.isEmpty()) return;
    double newPos = 0.0;
    m_audioEngine->seek(newPos);
    m_mpvWidget->seek(newPos);
}

void MainWindow::onNextTrack()
{
    if (m_destroyed || m_currentFilePath.isEmpty()) return;
    double newPos = m_mpvWidget->duration();
    m_audioEngine->seek(newPos);
    m_mpvWidget->seek(newPos);
}

void MainWindow::updateTitleBar()
{
    if (m_titleBar) {
        QString t = windowTitle();
        m_titleBar->setFileName(t.isEmpty() ? "EchoPlayer" : t);
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        m_audioTopMenu->setTitle(tr("声音"));
        m_audioMenu->setTitle(tr("选择声音"));
    } else if (event->type() == QEvent::WindowStateChange) {
        bool max = isMaximized();
        m_titleBar->updateMaximizeIcon(max);
        if (!isFullScreen()) {
            m_edgeTimer->stop();
            m_titleVisible = true;
            m_ctrlVisible = true;
            repositionBars();
        }
    }
    QMainWindow::changeEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (m_destroyed) return false;

    if (obj == m_mpvWidget && event->type() == QEvent::ContextMenu) {
        auto *ce = static_cast<QContextMenuEvent *>(event);
        if (m_contextMenu)
            m_contextMenu->popup(ce->globalPos());
        return true;
    }

    if (obj == m_mpvWidget && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            onToggleFullscreen();
            return true;
        }
        if (ke->key() == Qt::Key_Escape && isFullScreen()) {
            onToggleFullscreen();
            return true;
        }
    }

    auto *prevBtn = m_ctrlBar ? m_ctrlBar->prevBtn() : nullptr;
    auto *nextBtn = m_ctrlBar ? m_ctrlBar->nextBtn() : nullptr;
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

void MainWindow::onAudioGroupTriggered(QAction *act)
{
    if (m_destroyed) return;
    m_audioEngine->selectAudioTrack(act->data().toInt());
}

void MainWindow::onChannelModeTriggered(QAction *act)
{
    if (m_destroyed) return;
    m_audioEngine->setChannelMode(act->data().toInt());
}

void MainWindow::onSpectrumTimer()
{
    if (m_destroyed || !m_audioEngine) return;
    m_audioEngine->triggerSpectrumUpdate();
}

QString MainWindow::formatTime(double seconds)
{
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(std::round(seconds));
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
