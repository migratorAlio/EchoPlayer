#pragma once

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    void setPlaying(bool playing);
    void setFileLoaded(bool loaded);
    void setPosition(double pos, double dur);
    void setDuration(double dur);
    void setVolume(int vol);
    void setMuted(bool muted);
    void setTimeText(const QString &text);
    void setCodecText(const QString &text);

    QSlider *seekSlider() const { return m_seekSlider; }
    QSlider *volumeSlider() const { return m_volumeSlider; }
    QToolButton *prevBtn() const { return m_prevBtn; }
    QToolButton *nextBtn() const { return m_nextBtn; }
    QToolButton *volBtn() const { return m_volBtn; }
    QToolButton *listBtn() const { return m_listBtn; }

    // Add widget to the end of button row (index = -1) or at a specific position
    void addToButtonRow(QWidget *w, int index = -1);

    void retranslateUi();

signals:
    void playPauseClicked();
    void stopClicked();
    void rewindClicked();
    void forwardClicked();
    void openFileClicked();
    void listClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildSeekRow(QVBoxLayout *parent);
    void buildButtonRow(QVBoxLayout *parent);

    QHBoxLayout *m_buttonRow = nullptr;

    QSlider *m_seekSlider = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QToolButton *m_volBtn = nullptr;
    QToolButton *m_playBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QToolButton *m_openBtn = nullptr;
    QToolButton *m_listBtn = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_codecLabel = nullptr;
};
