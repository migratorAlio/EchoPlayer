#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QActionGroup>
#include <QPoint>
#include <QPropertyAnimation>
#include <QProcess>
#include <QTimer>
#include <QLabel>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>
#include <QByteArray>
#include <QImage>
#include <QSystemTrayIcon>

#include "core/types.h"

#include <atomic>

class MpvWidget;
class AudioEngine;
class TitleBar;
class ControlBar;
class SpectrumWidget;
class MprisController;
class ScreenSaverInhibitor;
class ThumbnailProvider;
class AdjustPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openMedia(const QString &path);
    void activateWithFiles(const QStringList &paths);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onOpenFile();
    void onAddSubtitle();
    void onPlayPause();
    void onStop();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderMoved(int value);
    void onPositionChanged(double pos);
    void onDurationChanged(double dur);
    void onPausedChanged(bool paused);
    void onMediaError(const QString &msg);
    void onFileLoaded();
    void onPlaybackFinished();

    void populateSubMenu();
    void populateAudioMenu();
    void onSubtitleGroupTriggered(QAction *act);
    void onAudioGroupTriggered(QAction *act);
    void onChannelModeTriggered(QAction *act);
    void onDecoderTriggered(QAction *act);
    void onLanguageSwitch(const QString &locale);
    void onTogglePlaylist();

    void onVolumeChanged(int value);
    void onToggleMute();
    void onToggleFullscreen();
    void onRewind();
    void onForward();
    void onPrevTrack();
    void onNextTrack();

    void onPlaylistStdoutReady();
    void onPlaylistProcessStarted();
    void onPlaylistCommand(const QStringList &tokens);

    void onOpenUrl();
    void onAddFilesToPlaylist();
    void onClearPlaylist();
    void onToggleShuffle();
    void onCycleRepeat();
    void onThumbReady(double seconds, const QImage &image);
    void onPreviewTimer();

    void onSpectrumTimer();
    void onLyricTimer();
    void onEdgeTimer();
    void onOsdTimer();

private:
    void setupUI();
    void setupMenus();
    void setupShortcuts();
    void setupDesktopIntegration();
    void setupSeekPreview();
    void setupAdjustPanel();
    void toggleAdjustPanel();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void syncTrayMenu();
    void captureTranslatables();
    void retranslateDynamic();

    void showOsd(const QString &text, int ms = 1500);
    void setSpeed(double speed);
    void seekRelative(double delta);
    void adjustVolume(int delta);
    void adjustBrightness(int delta);
    void adjustSubDelay(double delta);
    void cycleAudioPreset();
    void applyAudioPreset(int index);
    void setAspect(const QString &aspect);
    void rotateVideo();
    void resetPicture();
    void screenshot();
    void toggleMediaInfo();
    void markABLoop();
    void updateCodecLabel();
    QString formatTime(double seconds) const;

    void applyPersistedPlayback();
    void rememberPosition();
    void playRelative(int delta);
    int nextPlaylistIndex(int delta) const;
    static QString barePath(const QString &p);
    static double cueOffset(const QString &p);
    static bool isSubtitleFile(const QString &p);
    static bool isNetworkUrl(const QString &p);
    static QString displayNameFor(const QString &p);
    QString repeatModeName() const;

    void publishMprisMetadata();
    void exportCoverArt(const QImage &poster);
    void setInhibitForPlayback(bool playing);
    void showSeekPreview(int sliderX, double seconds);
    void hideSeekPreview();

    void launchPlaylistApp();
    void sendPlaylist();
    void sendPlaylistGeometry();
    void sendPlaylistLanguage();
    void sendCurrentFile();
    void closePlaylist();
    void scanPlaylist(const QString &filePath);
    void onPlaylistFileSelected(const QString &path);
    bool parseCueFile(const QString &cuePath, QList<CueTrack> &tracks);
    void applyCueTracks();

    void loadPlaylistState();
    void savePlaylistState();
    void addFilesToPlaylist(const QStringList &paths);
    void removeFromPlaylist(int index);
    void movePlaylistItem(int from, int to);
    void setPlaylistManual(bool manual);
    void sendPlaylistMode();

    void showTitleBar();
    void hideTitleBar();
    void showCtrlBar();
    void hideCtrlBar();
    void repositionBars();
    void updateTitleBar();

    MpvWidget *m_mpvWidget = nullptr;
    AudioEngine *m_audioEngine = nullptr;
    TitleBar *m_titleBar = nullptr;
    ControlBar *m_ctrlBar = nullptr;
    SpectrumWidget *m_spectrumWidget = nullptr;
    QLabel *m_lyricLabel = nullptr;
    QLabel *m_posterLabel = nullptr;
    QLabel *m_osdLabel = nullptr;
    QLabel *m_infoLabel = nullptr;

    QMenu *m_contextMenu = nullptr;
    QMenu *m_videoMenu = nullptr;
    QMenu *m_decoderMenu = nullptr;
    QMenu *m_aspectMenu = nullptr;
    QMenu *m_subMenu = nullptr;
    QMenu *m_subTrackMenu = nullptr;
    QMenu *m_audioTopMenu = nullptr;
    QMenu *m_audioMenu = nullptr;
    QMenu *m_channelMenu = nullptr;
    QMenu *m_presetMenu = nullptr;
    QActionGroup *m_subGroup = nullptr;
    QActionGroup *m_audioGroup = nullptr;
    QActionGroup *m_channelModeGroup = nullptr;
    QActionGroup *m_decoderGroup = nullptr;
    QActionGroup *m_aspectGroup = nullptr;
    QActionGroup *m_presetGroup = nullptr;

    bool m_sliderDragging = false;
    bool m_muted = false;
    int m_volumeBeforeMute = 100;
    double m_speed = 1.0;
    int m_rotation = 0;
    double m_abA = -1.0;
    int m_presetIndex = 0;
    // Fine-tuning values (equalizer, subtitles, video) live in AdjustPanel and
    // are persisted under QSettings "adjust/*"; MainWindow only keeps an index
    // into its preset menu here.
    bool m_infoVisible = false;
    double m_pendingSeek = -1.0;
    std::atomic<bool> m_destroyed{false};

    QPropertyAnimation *m_titleAnim = nullptr;
    QPropertyAnimation *m_ctrlAnim = nullptr;
    bool m_titleVisible = true;
    bool m_ctrlVisible = true;
    int m_titleBarHeight = 32;
    int m_ctrlBarHeight = 0;

    QProcess *m_playlistProcess = nullptr;
    QStringList m_playlistPaths;
    QStringList m_playlistNames;
    QString m_currentPlayingPath;
    QByteArray m_playlistReadBuf;
    QList<CueTrack> m_cueTracks;

    QTimer *m_spectrumTimer = nullptr;
    QTimer *m_lyricTimer = nullptr;
    QTimer *m_edgeTimer = nullptr;
    QTimer *m_osdTimer = nullptr;
    QTimer *m_previewTimer = nullptr;

    // Desktop integration
    MprisController *m_mpris = nullptr;
    ScreenSaverInhibitor *m_inhibitor = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayPlayAction = nullptr;
    QAction *m_trayOnCloseAction = nullptr;
    QAction *m_shuffleAction = nullptr;
    QAction *m_repeatAction = nullptr;
    bool m_trayOnClose = true;
    bool m_forceQuit = false;
    bool m_trayNoticeShown = false;

    // Seek-bar hover preview
    ThumbnailProvider *m_thumbs = nullptr;
    AdjustPanel *m_adjustPanel = nullptr;
    QWidget *m_seekPreview = nullptr;
    QLabel *m_seekPreviewImage = nullptr;
    QLabel *m_seekPreviewTime = nullptr;
    int m_previewX = 0;
    double m_previewSeconds = -1.0;

    // Stream + playlist state
    bool m_currentIsStream = false;
    bool m_mprisDurationKnown = false;
    QString m_currentTitle;
    QString m_currentArtUrl;
    int m_repeatMode = 0;      // 0 = off, 1 = whole playlist, 2 = one file
    bool m_shuffle = false;
    bool m_playlistManual = false;

    QString m_currentFilePath;
    QString m_lastLyricLine;

    QSettings *m_settings = nullptr;
    QVariantMap m_resumeMap;
    QStringList m_recentFiles;
    QString m_hwdec = QStringLiteral("auto");
    QString m_locale = QStringLiteral("zh_CN");
};
