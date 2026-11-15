#include "ui/adjustpanel.h"
#include "ui/mpvwidget.h"
#include "i18n/languagemanager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDebug>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTabWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

constexpr int kBandCount = 10;
const int kFreq[kBandCount] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
const char *kVideoKeys[5] = {"brightness", "contrast", "saturation", "gamma", "hue"};
// Values are in 0.1 dB units; the sliders use the same scale.
const int kPresetGains[4][kBandCount] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {60, 50, 40, 20, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 20, 40, 50, 60, 60},
    {-20, -10, 0, 10, 30, 40, 30, 10, 0, -10}
};

QString freqName(int f)
{
    return f >= 1000 ? QString::number(f / 1000) + QStringLiteral("k")
                     : QString::number(f);
}

QString dbText(int value)
{
    return QString::number(value / 10.0, 'f', 1) + QStringLiteral(" dB");
}

} // namespace

AdjustPanel::AdjustPanel(MpvWidget *mpv, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_mpv(mpv)
    , m_settings(settings)
{
    setModal(false);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(600, 560);

    buildUi();
    retranslateUi();
    loadState();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged,
            this, &AdjustPanel::retranslateUi);
}

void AdjustPanel::buildUi()
{
    m_lab.resize(L_COUNT);
    m_group.resize(G_COUNT);
    m_videoNames.resize(5);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    // ------------------------------------------------------------------ audio
    auto *audioPage = new QWidget(m_tabs);
    auto *audioLayout = new QVBoxLayout(audioPage);

    m_group[GEQ] = new QGroupBox(audioPage);
    auto *eqGrid = new QGridLayout(m_group[GEQ]);
    eqGrid->setVerticalSpacing(2);
    for (int i = 0; i < kBandCount; ++i) {
        auto *name = new QLabel(freqName(kFreq[i]), m_group[GEQ]);
        name->setAlignment(Qt::AlignHCenter);
        eqGrid->addWidget(name, 0, i);

        auto *slider = new QSlider(Qt::Vertical, m_group[GEQ]);
        slider->setRange(-120, 120);
        slider->setSingleStep(5);
        slider->setPageStep(10);
        slider->setFixedHeight(110);
        eqGrid->addWidget(slider, 1, i, Qt::AlignHCenter);
        m_eq.append(slider);

        auto *value = new QLabel(m_group[GEQ]);
        value->setAlignment(Qt::AlignHCenter);
        value->setFixedWidth(52);
        eqGrid->addWidget(value, 2, i);
        m_eqValues.append(value);

        connect(slider, &QSlider::valueChanged, this, &AdjustPanel::onEquationChanged);
    }
    auto *presetRow = new QHBoxLayout;
    m_lab[LPreset] = new QLabel(m_group[GEQ]);
    m_eqPreset = new QComboBox(m_group[GEQ]);
    presetRow->addWidget(m_lab[LPreset]);
    presetRow->addWidget(m_eqPreset, 1);
    eqGrid->addLayout(presetRow, 3, 0, 1, kBandCount);
    audioLayout->addWidget(m_group[GEQ], 1);

    m_group[GAudioExtra] = new QGroupBox(audioPage);
    auto *extraGrid = new QGridLayout(m_group[GAudioExtra]);
    m_normalize = new QCheckBox(m_group[GAudioExtra]);
    extraGrid->addWidget(m_normalize, 0, 0, 1, 3);

    m_lab[LBalance] = new QLabel(m_group[GAudioExtra]);
    m_balance = new QSlider(Qt::Horizontal, m_group[GAudioExtra]);
    m_balance->setRange(-100, 100);
    m_balanceValue = new QLabel(m_group[GAudioExtra]);
    m_balanceValue->setFixedWidth(56);
    extraGrid->addWidget(m_lab[LBalance], 1, 0);
    extraGrid->addWidget(m_balance, 1, 1);
    extraGrid->addWidget(m_balanceValue, 1, 2);

    m_lab[LAudioDelay] = new QLabel(m_group[GAudioExtra]);
    m_audioDelay = new QDoubleSpinBox(m_group[GAudioExtra]);
    m_audioDelay->setRange(-10.0, 10.0);
    m_audioDelay->setSingleStep(0.1);
    m_audioDelay->setDecimals(2);
    m_audioDelay->setSuffix(QStringLiteral(" s"));
    extraGrid->addWidget(m_lab[LAudioDelay], 2, 0);
    extraGrid->addWidget(m_audioDelay, 2, 1, 1, 2);

    m_resetButtons[0] = new QPushButton(m_group[GAudioExtra]);
    extraGrid->addWidget(m_resetButtons[0], 3, 0, 1, 3, Qt::AlignRight);
    audioLayout->addWidget(m_group[GAudioExtra]);

    m_tabs->addTab(audioPage, QString());

    // --------------------------------------------------------------- subtitles
    auto *subPage = new QWidget(m_tabs);
    auto *subLayout = new QVBoxLayout(subPage);

    m_group[GSubGeom] = new QGroupBox(subPage);
    auto *geomGrid = new QGridLayout(m_group[GSubGeom]);
    m_lab[LSubScale] = new QLabel(m_group[GSubGeom]);
    m_subScale = new QDoubleSpinBox(m_group[GSubGeom]);
    m_subScale->setRange(0.3, 3.0);
    m_subScale->setSingleStep(0.1);
    m_subScale->setDecimals(2);
    m_lab[LSubDelay] = new QLabel(m_group[GSubGeom]);
    m_subDelaySpin = new QDoubleSpinBox(m_group[GSubGeom]);
    m_subDelaySpin->setRange(-10.0, 10.0);
    m_subDelaySpin->setSingleStep(0.1);
    m_subDelaySpin->setDecimals(2);
    m_subDelaySpin->setSuffix(QStringLiteral(" s"));
    m_lab[LSubPos] = new QLabel(m_group[GSubGeom]);
    m_subPos = new QSlider(Qt::Horizontal, m_group[GSubGeom]);
    m_subPos->setRange(0, 150);
    m_subValues[0] = new QLabel(m_group[GSubGeom]);
    m_subValues[0]->setFixedWidth(40);
    geomGrid->addWidget(m_lab[LSubScale], 0, 0);
    geomGrid->addWidget(m_subScale, 0, 1, 1, 2);
    geomGrid->addWidget(m_lab[LSubDelay], 1, 0);
    geomGrid->addWidget(m_subDelaySpin, 1, 1, 1, 2);
    geomGrid->addWidget(m_lab[LSubPos], 2, 0);
    geomGrid->addWidget(m_subPos, 2, 1);
    geomGrid->addWidget(m_subValues[0], 2, 2);
    subLayout->addWidget(m_group[GSubGeom]);

    m_group[GSubStyle] = new QGroupBox(subPage);
    auto *styleGrid = new QGridLayout(m_group[GSubStyle]);
    m_lab[LFont] = new QLabel(m_group[GSubStyle]);
    m_font = new QFontComboBox(m_group[GSubStyle]);
    m_bold = new QCheckBox(m_group[GSubStyle]);
    m_italic = new QCheckBox(m_group[GSubStyle]);
    m_lab[LAlign] = new QLabel(m_group[GSubStyle]);
    m_align = new QComboBox(m_group[GSubStyle]);
    styleGrid->addWidget(m_lab[LFont], 0, 0);
    styleGrid->addWidget(m_font, 0, 1);
    styleGrid->addWidget(m_bold, 0, 2);
    styleGrid->addWidget(m_italic, 0, 3);
    styleGrid->addWidget(m_lab[LAlign], 1, 0);
    styleGrid->addWidget(m_align, 1, 1, 1, 3);

    // The three style sliders are laid out explicitly (the position slider
    // already lives on the geometry page).
    const int ids[3] = {LBorder, LShadow, LBlur};
    const int maxes[3] = {20, 20, 40};
    QSlider *styleSliders[3] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < 3; ++i) {
        m_lab[ids[i]] = new QLabel(m_group[GSubStyle]);
        styleSliders[i] = new QSlider(Qt::Horizontal, m_group[GSubStyle]);
        styleSliders[i]->setRange(0, maxes[i]);
        m_subValues[i + 1] = new QLabel(m_group[GSubStyle]);
        m_subValues[i + 1]->setFixedWidth(40);
        styleGrid->addWidget(m_lab[ids[i]], 2 + i, 0);
        styleGrid->addWidget(styleSliders[i], 2 + i, 1, 1, 2);
        styleGrid->addWidget(m_subValues[i + 1], 2 + i, 3);
    }
    m_borderSize = styleSliders[0];
    m_shadowOffset = styleSliders[1];
    m_blur = styleSliders[2];
    subLayout->addWidget(m_group[GSubStyle]);

    m_group[GSubColor] = new QGroupBox(subPage);
    auto *colorRow = new QHBoxLayout(m_group[GSubColor]);
    const int colorIds[3] = {LColorText, LColorBorder, LColorShadow};
    for (int i = 0; i < 3; ++i) {
        m_lab[colorIds[i]] = new QLabel(m_group[GSubColor]);
        m_colorButtons[i] = new QPushButton(m_group[GSubColor]);
        colorRow->addWidget(m_lab[colorIds[i]]);
        colorRow->addWidget(m_colorButtons[i], 1);
        connect(m_colorButtons[i], &QPushButton::clicked, this,
                [this, i] { pickColor(i); });
    }
    subLayout->addWidget(m_group[GSubColor]);

    m_resetButtons[1] = new QPushButton(subPage);
    subLayout->addWidget(m_resetButtons[1], 0, Qt::AlignRight);
    subLayout->addStretch(1);
    m_tabs->addTab(subPage, QString());

    // ------------------------------------------------------------------- video
    auto *videoPage = new QWidget(m_tabs);
    auto *videoLayout = new QVBoxLayout(videoPage);
    m_group[GVideo] = new QGroupBox(videoPage);
    auto *videoGrid = new QGridLayout(m_group[GVideo]);
    for (int i = 0; i < 5; ++i) {
        m_videoNames[i] = new QLabel(m_group[GVideo]);
        auto *slider = new QSlider(Qt::Horizontal, m_group[GVideo]);
        slider->setRange(-100, 100);
        slider->setSingleStep(1);
        auto *value = new QLabel(m_group[GVideo]);
        value->setFixedWidth(44);
        m_video.append(slider);
        m_videoValues.append(value);
        videoGrid->addWidget(m_videoNames[i], i, 0);
        videoGrid->addWidget(slider, i, 1);
        videoGrid->addWidget(value, i, 2);
        connect(slider, &QSlider::valueChanged, this, &AdjustPanel::onVideoChanged);
    }
    videoLayout->addWidget(m_group[GVideo]);
    m_resetButtons[2] = new QPushButton(videoPage);
    videoLayout->addWidget(m_resetButtons[2], 0, Qt::AlignRight);
    videoLayout->addStretch(1);
    m_tabs->addTab(videoPage, QString());

    for (int i = 0; i < 3; ++i)
        connect(m_resetButtons[i], &QPushButton::clicked, this,
                [this, i] { resetPage(i); });

    auto *bottom = new QHBoxLayout;
    bottom->addStretch(1);
    m_closeBtn = new QPushButton(this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    bottom->addWidget(m_closeBtn);
    root->addLayout(bottom);

    // ---- signal wiring (all changes are live) ----
    connect(m_eqPreset, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_loading)
            applyPreset(idx);
    });
    connect(m_normalize, &QCheckBox::toggled, this, &AdjustPanel::onAudioChanged);
    connect(m_balance, &QSlider::valueChanged, this, &AdjustPanel::onAudioChanged);
    connect(m_audioDelay, &QDoubleSpinBox::valueChanged, this, &AdjustPanel::onAudioChanged);

    connect(m_subScale, &QDoubleSpinBox::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_subDelaySpin, &QDoubleSpinBox::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_subPos, &QSlider::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_borderSize, &QSlider::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_shadowOffset, &QSlider::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_blur, &QSlider::valueChanged, this, &AdjustPanel::onSubChanged);
    connect(m_font, &QFontComboBox::currentFontChanged, this, &AdjustPanel::onSubChanged);
    connect(m_bold, &QCheckBox::toggled, this, &AdjustPanel::onSubChanged);
    connect(m_italic, &QCheckBox::toggled, this, &AdjustPanel::onSubChanged);
    connect(m_align, &QComboBox::currentIndexChanged, this, &AdjustPanel::onSubChanged);
}

void AdjustPanel::loadState()
{
    m_loading = true;
    auto val = [this](const QString &key, const QVariant &def) {
        return m_settings ? m_settings->value(QStringLiteral("adjust/") + key, def) : def;
    };

    for (int i = 0; i < kBandCount; ++i)
        m_eq[i]->setValue(qBound(-120, val(QString("eq%1").arg(i), 0).toInt(), 120));
    const int preset = qBound(0, val(QStringLiteral("preset"), 0).toInt(), 3);
    if (preset > 0) {
        for (int i = 0; i < kBandCount; ++i)
            m_eq[i]->setValue(kPresetGains[preset][i]);
    }
    m_normalize->setChecked(val(QStringLiteral("normalize"), false).toBool());
    m_balance->setValue(qBound(-100, val(QStringLiteral("balance"), 0).toInt(), 100));
    m_audioDelay->setValue(qBound(-10.0, val(QStringLiteral("audioDelay"), 0.0).toDouble(), 10.0));

    m_subScale->setValue(qBound(0.3, val(QStringLiteral("subScale"), 1.0).toDouble(), 3.0));
    m_subDelaySpin->setValue(qBound(-10.0, val(QStringLiteral("subDelay"), 0.0).toDouble(), 10.0));
    m_subPos->setValue(qBound(0, val(QStringLiteral("subPos"), 100).toInt(), 150));
    m_borderSize->setValue(qBound(0, val(QStringLiteral("borderSize"), 6).toInt(), 20));
    m_shadowOffset->setValue(qBound(0, val(QStringLiteral("shadowOffset"), 0).toInt(), 20));
    m_blur->setValue(qBound(0, val(QStringLiteral("blur"), 0).toInt(), 40));
    m_bold->setChecked(val(QStringLiteral("bold"), false).toBool());
    m_italic->setChecked(val(QStringLiteral("italic"), false).toBool());
    m_align->setCurrentIndex(qBound(0, val(QStringLiteral("align"), 1).toInt(), 2));
    const QString family = val(QStringLiteral("font"), QString()).toString();
    if (!family.isEmpty())
        m_font->setCurrentFont(QFont(family));

    m_colors[0] = QColor(val(QStringLiteral("colorText"), QStringLiteral("#FFFFFF")).toString());
    m_colors[1] = QColor(val(QStringLiteral("colorBorder"), QStringLiteral("#000000")).toString());
    m_colors[2] = QColor(val(QStringLiteral("colorShadow"), QStringLiteral("#000000")).toString());
    for (QColor &c : m_colors)
        if (!c.isValid())
            c = QColor(Qt::white);

    for (int i = 0; i < 5; ++i)
        m_video[i]->setValue(qBound(-100, val(QString("video%1").arg(i), 0).toInt(), 100));

    {
        QSignalBlocker blocker(m_eqPreset);
        m_eqPreset->setCurrentIndex(preset);
    }
    m_loading = false;
    updateValueLabels();
    updateColorButtons();
}

void AdjustPanel::saveState()
{
    if (!m_settings || m_loading)
        return;
    auto set = [this](const QString &key, const QVariant &v) {
        m_settings->setValue(QStringLiteral("adjust/") + key, v);
    };
    for (int i = 0; i < kBandCount; ++i)
        set(QString("eq%1").arg(i), m_eq[i]->value());
    set(QStringLiteral("preset"), m_eqPreset->currentIndex());
    set(QStringLiteral("normalize"), m_normalize->isChecked());
    set(QStringLiteral("balance"), m_balance->value());
    set(QStringLiteral("audioDelay"), m_audioDelay->value());
    set(QStringLiteral("subScale"), m_subScale->value());
    set(QStringLiteral("subDelay"), m_subDelaySpin->value());
    set(QStringLiteral("subPos"), m_subPos->value());
    set(QStringLiteral("borderSize"), m_borderSize->value());
    set(QStringLiteral("shadowOffset"), m_shadowOffset->value());
    set(QStringLiteral("blur"), m_blur->value());
    set(QStringLiteral("bold"), m_bold->isChecked());
    set(QStringLiteral("italic"), m_italic->isChecked());
    set(QStringLiteral("align"), m_align->currentIndex());
    set(QStringLiteral("font"), m_font->currentFont().family());
    const char *colorKeys[3] = {"colorText", "colorBorder", "colorShadow"};
    for (int i = 0; i < 3; ++i)
        set(QString::fromUtf8(colorKeys[i]), m_colors[i].name());
    for (int i = 0; i < 5; ++i)
        set(QString("video%1").arg(i), m_video[i]->value());
}

void AdjustPanel::pushAudio()
{
    if (!m_mpv || !m_mpv->mpvReady())
        return;

    QStringList chain;
    if (m_normalize->isChecked())
        chain << QStringLiteral("dynaudnorm=f=250:g=3");
    for (int i = 0; i < kBandCount; ++i) {
        const double db = m_eq[i]->value() / 10.0;
        if (qAbs(db) > 0.05)
            chain << QStringLiteral("equalizer=f=%1:t=q:w=1:g=%2")
                     .arg(kFreq[i]).arg(db, 0, 'f', 1);
    }
    const QString af = chain.isEmpty()
        ? QString()
        : QStringLiteral("lavfi=[") + chain.join(QLatin1Char(',')) + QLatin1Char(']');
    m_mpv->setAudioFilter(af);
    if (af != m_lastAf) {
        m_lastAf = af;
        qDebug() << "[adjust] audio filter:" << af;
    }
    m_mpv->setProperty(QStringLiteral("balance"), m_balance->value() / 100.0);
    m_mpv->setProperty(QStringLiteral("audio-delay"), m_audioDelay->value());
}

void AdjustPanel::pushSubtitles()
{
    if (!m_mpv || !m_mpv->mpvReady())
        return;
    m_mpv->setProperty(QStringLiteral("sub-scale"), m_subScale->value());
    m_mpv->setProperty(QStringLiteral("sub-delay"), m_subDelaySpin->value());
    m_mpv->setProperty(QStringLiteral("sub-pos"), QVariant::fromValue<qlonglong>(m_subPos->value()));
    m_mpv->setProperty(QStringLiteral("sub-font"), m_font->currentFont().family());
    m_mpv->setProperty(QStringLiteral("sub-bold"), m_bold->isChecked());
    m_mpv->setProperty(QStringLiteral("sub-italic"), m_italic->isChecked());
    m_mpv->setProperty(QStringLiteral("sub-align-x"),
                       QVariant::fromValue<qlonglong>(m_align->currentIndex() * 50));
    m_mpv->setProperty(QStringLiteral("sub-border-size"), m_borderSize->value() / 2.0);
    m_mpv->setProperty(QStringLiteral("sub-shadow-offset"), m_shadowOffset->value() / 2.0);
    m_mpv->setProperty(QStringLiteral("sub-blur"), m_blur->value() / 2.0);
    m_mpv->setProperty(QStringLiteral("sub-color"), m_colors[0].name());
    m_mpv->setProperty(QStringLiteral("sub-border-color"), m_colors[1].name());
    m_mpv->setProperty(QStringLiteral("sub-shadow-color"), m_colors[2].name());
}

void AdjustPanel::pushVideo()
{
    if (!m_mpv || !m_mpv->mpvReady())
        return;
    for (int i = 0; i < 5; ++i)
        m_mpv->setProperty(QString::fromUtf8(kVideoKeys[i]),
                           QVariant::fromValue<qlonglong>(m_video[i]->value()));
}

void AdjustPanel::applyAll()
{
    pushAudio();
    pushSubtitles();
    pushVideo();
}

void AdjustPanel::applyPreset(int index)
{
    const int idx = qBound(0, index, 3);
    m_loading = true;
    for (int i = 0; i < kBandCount; ++i)
        m_eq[i]->setValue(kPresetGains[idx][i]);
    {
        QSignalBlocker blocker(m_eqPreset);
        m_eqPreset->setCurrentIndex(idx);
    }
    m_loading = false;
    updateValueLabels();
    saveState();
    pushAudio();
}

void AdjustPanel::updateValueLabels()
{
    for (int i = 0; i < kBandCount; ++i)
        m_eqValues[i]->setText(dbText(m_eq[i]->value()));
    m_balanceValue->setText(QString::number(m_balance->value() / 100.0, 'f', 2));
    m_subValues[0]->setText(QString::number(m_subPos->value()));
    m_subValues[1]->setText(QString::number(m_borderSize->value() / 2.0, 'f', 1));
    m_subValues[2]->setText(QString::number(m_shadowOffset->value() / 2.0, 'f', 1));
    m_subValues[3]->setText(QString::number(m_blur->value() / 2.0, 'f', 1));
    for (int i = 0; i < 5; ++i)
        m_videoValues[i]->setText(QString::number(m_video[i]->value()));
}

void AdjustPanel::updateColorButtons()
{
    for (int i = 0; i < 3; ++i) {
        const QColor &c = m_colors[i];
        const bool bright = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000 > 128;
        m_colorButtons[i]->setText(c.name(QColor::HexRgb).toUpper());
        m_colorButtons[i]->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2; padding: 4px 6px;")
                .arg(c.name(), bright ? QStringLiteral("#111111") : QStringLiteral("#eeeeee")));
    }
}

void AdjustPanel::pickColor(int which)
{
    const QColor chosen = QColorDialog::getColor(m_colors[which], this, tr("选择颜色"));
    if (!chosen.isValid())
        return;
    m_colors[which] = chosen;
    updateColorButtons();
    onSubChanged();
}

void AdjustPanel::onEquationChanged()
{
    if (m_loading)
        return;
    {
        QSignalBlocker blocker(m_eqPreset);
        m_eqPreset->setCurrentIndex(0);
    }
    emit presetOverridden();
    onAudioChanged();
}

void AdjustPanel::onAudioChanged()
{
    if (m_loading)
        return;
    updateValueLabels();
    saveState();
    pushAudio();
}

void AdjustPanel::onSubChanged()
{
    if (m_loading)
        return;
    updateValueLabels();
    saveState();
    pushSubtitles();
}

void AdjustPanel::onVideoChanged()
{
    if (m_loading)
        return;
    updateValueLabels();
    saveState();
    pushVideo();
}

void AdjustPanel::resetPage(int page)
{
    m_loading = true;
    if (page == 0) {
        for (int i = 0; i < kBandCount; ++i)
            m_eq[i]->setValue(0);
        m_normalize->setChecked(false);
        m_balance->setValue(0);
        m_audioDelay->setValue(0.0);
        QSignalBlocker blocker(m_eqPreset);
        m_eqPreset->setCurrentIndex(0);
    } else if (page == 1) {
        m_subScale->setValue(1.0);
        m_subDelaySpin->setValue(0.0);
        m_subPos->setValue(100);
        m_borderSize->setValue(6);
        m_shadowOffset->setValue(0);
        m_blur->setValue(0);
        m_bold->setChecked(false);
        m_italic->setChecked(false);
        m_align->setCurrentIndex(1);
        m_colors[0] = QColor(QStringLiteral("#FFFFFF"));
        m_colors[1] = QColor(QStringLiteral("#000000"));
        m_colors[2] = QColor(QStringLiteral("#000000"));
        updateColorButtons();
    } else {
        for (int i = 0; i < 5; ++i)
            m_video[i]->setValue(0);
    }
    m_loading = false;
    updateValueLabels();
    saveState();
    applyAll();
}

void AdjustPanel::bumpBrightness(int delta)
{
    m_video[0]->setValue(qBound(-100, m_video[0]->value() + delta, 100));
}

void AdjustPanel::bumpSubScale(double delta)
{
    m_subScale->setValue(qBound(0.3, m_subScale->value() + delta, 3.0));
}

void AdjustPanel::bumpSubDelay(double delta)
{
    m_subDelaySpin->setValue(qBound(-10.0, m_subDelaySpin->value() + delta, 10.0));
}

void AdjustPanel::resetVideo()
{
    resetPage(2);
}

int AdjustPanel::brightness() const { return m_video[0]->value(); }
double AdjustPanel::subScale() const { return m_subScale->value(); }
double AdjustPanel::subDelay() const { return m_subDelaySpin->value(); }

void AdjustPanel::retranslateUi()
{
    setWindowTitle(tr("调整面板"));
    m_tabs->setTabText(0, tr("音频"));
    m_tabs->setTabText(1, tr("字幕样式"));
    m_tabs->setTabText(2, tr("画面"));

    m_group[GEQ]->setTitle(tr("图形均衡器"));
    m_group[GAudioExtra]->setTitle(tr("其他音频设置"));
    m_group[GSubGeom]->setTitle(tr("大小与位置"));
    m_group[GSubStyle]->setTitle(tr("字体与样式"));
    m_group[GSubColor]->setTitle(tr("颜色"));
    m_group[GVideo]->setTitle(tr("画面调整"));

    m_lab[LPreset]->setText(tr("音效预设"));
    m_lab[LBalance]->setText(tr("声道平衡"));
    m_lab[LAudioDelay]->setText(tr("音轨延迟"));
    m_lab[LSubScale]->setText(tr("字号"));
    m_lab[LSubDelay]->setText(tr("字幕延迟"));
    m_lab[LSubPos]->setText(tr("垂直位置"));
    m_lab[LFont]->setText(tr("字体"));
    m_lab[LAlign]->setText(tr("水平对齐"));
    m_lab[LBorder]->setText(tr("描边粗细"));
    m_lab[LShadow]->setText(tr("阴影偏移"));
    m_lab[LBlur]->setText(tr("边缘模糊"));
    m_lab[LColorText]->setText(tr("文字"));
    m_lab[LColorBorder]->setText(tr("描边"));
    m_lab[LColorShadow]->setText(tr("阴影"));

    m_normalize->setText(tr("音量归一化"));
    m_bold->setText(tr("粗体"));
    m_italic->setText(tr("斜体"));

    const QStringList videoNames = {
        tr("亮度"), tr("对比度"), tr("饱和度"), tr("伽马"), tr("色相")
    };
    for (int i = 0; i < 5; ++i)
        m_videoNames[i]->setText(videoNames.at(i));

    {
        QSignalBlocker blocker(m_eqPreset);
        const int idx = m_eqPreset->currentIndex();
        m_eqPreset->clear();
        m_eqPreset->addItems({tr("平直"), tr("低音增强"), tr("高音增强"), tr("人声增强")});
        m_eqPreset->setCurrentIndex(qBound(0, idx, 3));
    }
    {
        QSignalBlocker blocker(m_align);
        const int idx = m_align->currentIndex();
        m_align->clear();
        m_align->addItems({tr("左"), tr("居中"), tr("右")});
        m_align->setCurrentIndex(qBound(0, idx, 2));
    }

    for (int i = 0; i < 3; ++i)
        m_resetButtons[i]->setText(tr("重置本页"));
    m_closeBtn->setText(tr("关闭"));
}
