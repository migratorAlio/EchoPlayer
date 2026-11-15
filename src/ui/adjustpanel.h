#pragma once

#include <QDialog>
#include <QColor>
#include <QVector>

class QTabWidget;
class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QGroupBox;
class QFontComboBox;
class QSettings;
class MpvWidget;

// Non-modal fine-tuning dialog: 10-band equalizer / audio extras, subtitle
// appearance, and video equalizer. Every widget writes straight to mpv
// properties and persists itself in QSettings under the "adjust/" group.
class AdjustPanel : public QDialog
{
    Q_OBJECT
public:
    explicit AdjustPanel(MpvWidget *mpv, QSettings *settings, QWidget *parent = nullptr);

    // Re-apply everything (called once per newly loaded file).
    void applyAll();
    // 0 = flat, 1 = bass, 2 = treble, 3 = vocal. Also used by the menu preset.
    void applyPreset(int index);
    void retranslateUi();

    int brightness() const;
    double subScale() const;
    double subDelay() const;
    void bumpBrightness(int delta);
    void bumpSubScale(double delta);
    void bumpSubDelay(double delta);
    void resetVideo();

signals:
    void presetOverridden();

private:
    enum Label {
        LPreset, LBalance, LAudioDelay,
        LSubScale, LSubDelay, LSubPos, LFont, LAlign, LBorder, LShadow, LBlur,
        LColorText, LColorBorder, LColorShadow,
        L_COUNT
    };
    enum Group { GEQ, GAudioExtra, GSubGeom, GSubStyle, GSubColor, GVideo, G_COUNT };

    void buildUi();
    void loadState();
    void saveState();
    void pushAudio();
    void pushSubtitles();
    void pushVideo();
    void updateValueLabels();
    void updateColorButtons();
    void pickColor(int which);
    void resetPage(int page);
    void onEquationChanged();
    void onAudioChanged();
    void onSubChanged();
    void onVideoChanged();

    MpvWidget *m_mpv = nullptr;
    QSettings *m_settings = nullptr;
    QTabWidget *m_tabs = nullptr;
    bool m_loading = false;

    QVector<QLabel *> m_lab;
    QVector<QGroupBox *> m_group;

    // --- audio tab ---
    static constexpr int kBands = 10;
    QVector<QSlider *> m_eq;
    QVector<QLabel *> m_eqValues;
    QComboBox *m_eqPreset = nullptr;
    QCheckBox *m_normalize = nullptr;
    QSlider *m_balance = nullptr;
    QLabel *m_balanceValue = nullptr;
    QDoubleSpinBox *m_audioDelay = nullptr;
    QString m_lastAf;

    // --- subtitle tab ---
    QDoubleSpinBox *m_subScale = nullptr;
    QDoubleSpinBox *m_subDelaySpin = nullptr;
    QSlider *m_subPos = nullptr;
    QSlider *m_borderSize = nullptr;
    QSlider *m_shadowOffset = nullptr;
    QSlider *m_blur = nullptr;
    QLabel *m_subValues[4] = {nullptr, nullptr, nullptr, nullptr};
    QFontComboBox *m_font = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_italic = nullptr;
    QComboBox *m_align = nullptr;
    QPushButton *m_colorButtons[3] = {nullptr, nullptr, nullptr};
    QColor m_colors[3];

    // --- video tab ---
    QVector<QSlider *> m_video;
    QVector<QLabel *> m_videoNames;
    QVector<QLabel *> m_videoValues;

    QPushButton *m_resetButtons[3] = {nullptr, nullptr, nullptr};
    QPushButton *m_closeBtn = nullptr;
};
