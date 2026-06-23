#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QActionGroup>
#include <QPoint>
#include <QPropertyAnimation>
#include <QProcess>
#include <QTimer>
#include <QLabel>
#include "core/types.h"
#include <atomic>

class MpvWidget;
class AudioEngine;
class TitleBar;
class ControlBar;
class SpectrumWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openMedia(const QString &path);

private slots:
    void onOpenFile();
    void onPlayPause();
    void onStop();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderMoved(int position);
    void onPositionChanged(double pos);
    void onDurationChanged(double dur);
    void onStateChanged(bool playing);
    void onMediaError(const QString &msg);
    void onFinished();
    void onAudioGroupTriggered(QAction *act);
    void onChannelModeTriggered(QAction *act);
    void onVolumeChanged(int value);
    void onToggleMute();
    void onToggleFullscreen();
    void onRewind();
    void onForward();
    void onPrevTrack();
    void onNextTrack();
    void onLanguageSwitch(const QString &locale);
    void onTogglePlaylist();

    // AudioEngine slots
    void onAudioStateChanged(bool playing);

    // QProcess slots
    void onPlaylistStdoutReady();
    void onPlaylistProcessStarted();

    // Spectrum & lyrics
    void onSpectrumTimer();
    void onLyricTimer();
    void onAvSyncTimer();
    void onEdgeTimer();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setupUI();
    void setupMenu();
    void populateSubMenu();
    void connectSignals();
    void launchPlaylistApp();
    void sendPlaylist();
    void sendPlaylistGeometry();
    void sendCurrentFile();
    void closePlaylist();
    void updateTitleBar();
    void showTitleBar();
    void hideTitleBar();
    void showCtrlBar();
    void hideCtrlBar();
    void repositionBars();
    void scanPlaylist(const QString &filePath);
    void onPlaylistFileSelected(const QString &path);
    bool parseCueFile(const QString &cuePath, QList<CueTrack> &tracks);
    void applyCueTracks();

    QString formatTime(double seconds);

    MpvWidget *m_mpvWidget = nullptr;
    AudioEngine *m_audioEngine = nullptr;
    TitleBar *m_titleBar = nullptr;
    ControlBar *m_ctrlBar = nullptr;
    SpectrumWidget *m_spectrumWidget = nullptr;
    QLabel *m_lyricLabel = nullptr;
    QLabel *m_posterLabel = nullptr;

    QMenu *m_contextMenu = nullptr;
    QMenu *m_videoMenu = nullptr;
    QMenu *m_decoderMenu = nullptr;
    QMenu *m_subMenu = nullptr;
    QActionGroup *m_subGroup = nullptr;
    QMenu *m_audioTopMenu = nullptr;
    QMenu *m_audioMenu = nullptr;
    QActionGroup *m_audioGroup = nullptr;
    QActionGroup *m_channelModeGroup = nullptr;
    QActionGroup *m_decoderGroup = nullptr;

    bool m_sliderDragging = false;
    bool m_muted = false;
    int m_volumeBeforeMute = 100;
    std::atomic<bool> m_destroyed{false};

    // Fullscreen overlay state
    QPropertyAnimation *m_titleAnim = nullptr;
    QPropertyAnimation *m_ctrlAnim = nullptr;
    bool m_titleVisible = true;
    bool m_ctrlVisible = true;
    int m_titleBarHeight = 32;
    int m_ctrlBarHeight = 0;

    // Playlist process & data
    QProcess *m_playlistProcess = nullptr;
    QStringList m_playlistPaths;
    QStringList m_playlistNames;
    QString m_currentPlayingPath;
    QByteArray m_playlistReadBuf;

    // Cue sheet
    QList<CueTrack> m_cueTracks;

    // Timers
    QTimer *m_spectrumTimer = nullptr;
    QTimer *m_lyricTimer = nullptr;
    QTimer *m_avSyncTimer = nullptr;
    QTimer *m_edgeTimer = nullptr;

    // File path
    QString m_currentFilePath;
    QString m_lastLyricLine;
};
