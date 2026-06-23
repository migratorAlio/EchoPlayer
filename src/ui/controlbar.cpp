#include "ui/controlbar.h"
#include <QEvent>
#include <QPainter>

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildSeekRow(layout);
    buildButtonRow(layout);
    retranslateUi();
}

void ControlBar::retranslateUi()
{
    m_volBtn->setToolTip(tr("切换静音"));
    m_playBtn->setToolTip(m_playBtn->text() == "⏸" ? tr("暂停") : tr("播放"));
    m_stopBtn->setToolTip(tr("停止"));
    m_prevBtn->setToolTip(tr("左键：快退\n右键：上一个"));
    m_nextBtn->setToolTip(tr("左键：快进\n右键：下一个"));
    m_openBtn->setToolTip(tr("打开文件"));
    m_listBtn->setToolTip(tr("列表"));
}

void ControlBar::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x22, 0x22, 0x22));
    QWidget::paintEvent(event);
}

void ControlBar::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void ControlBar::buildSeekRow(QVBoxLayout *parent)
{
    auto *row = new QHBoxLayout();
    row->setContentsMargins(3, 0, 10, 0);
    row->setSpacing(4);

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #1a9fff; width: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #1a9fff; border-radius: 2px; }");
    row->addWidget(m_seekSlider, 1);

    m_volBtn = new QToolButton(this);
    m_volBtn->setText("🔊");
    m_volBtn->setStyleSheet(
        "QToolButton { color: #ccc; font-size: 14px; border: none; padding: 4px 8px; }"
        "QToolButton:hover { background: #2d2d5e; }");

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 200);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(100);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #ccc; width: 10px; margin: -3px 0; border-radius: 5px; }"
        "QSlider::sub-page:horizontal { background: #1a9fff; border-radius: 2px; }");

    row->addWidget(m_volBtn);
    row->addWidget(m_volumeSlider);
    parent->addLayout(row);
}

void ControlBar::buildButtonRow(QVBoxLayout *parent)
{
    m_buttonRow = new QHBoxLayout();
    m_buttonRow->setContentsMargins(8, 2, 10, 2);
    m_buttonRow->setSpacing(4);

    m_playBtn = new QToolButton(this);
    m_playBtn->setText("▶");
    m_playBtn->setEnabled(false);
    m_playBtn->setFixedWidth(36);

    m_stopBtn = new QToolButton(this);
    m_stopBtn->setText("⏹");
    m_stopBtn->setEnabled(false);

    m_prevBtn = new QToolButton(this);
    m_prevBtn->setText("⏮");
    m_prevBtn->setEnabled(false);

    m_nextBtn = new QToolButton(this);
    m_nextBtn->setText("⏭");
    m_nextBtn->setEnabled(false);

    m_buttonRow->addWidget(m_playBtn, 0, Qt::AlignVCenter);
    m_buttonRow->addWidget(m_stopBtn, 0, Qt::AlignVCenter);
    m_buttonRow->addWidget(m_prevBtn, 0, Qt::AlignVCenter);
    m_buttonRow->addWidget(m_nextBtn, 0, Qt::AlignVCenter);

    m_openBtn = new QToolButton(this);
    m_openBtn->setText("📂");
    m_openBtn->setStyleSheet(
        "QToolButton { color: #ccc; font-size: 14px; border: none; padding: 4px 8px; }"
        "QToolButton:hover { background: #2d2d5e; }");
    m_buttonRow->addWidget(m_openBtn, 0, Qt::AlignVCenter);

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    m_timeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_buttonRow->addWidget(m_timeLabel, 0, Qt::AlignVCenter);

    m_codecLabel = new QLabel(this);
    m_codecLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_codecLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_buttonRow->addWidget(m_codecLabel, 0, Qt::AlignVCenter);

    m_buttonRow->addStretch(1);

    m_listBtn = new QToolButton(this);
    m_listBtn->setText("📋");
    m_listBtn->setToolTip(tr("列表"));
    m_listBtn->setStyleSheet(
        "QToolButton { color: #ccc; font-size: 14px; border: none; padding: 4px 8px; }"
        "QToolButton:hover { background: #2d2d5e; }");
    m_buttonRow->addWidget(m_listBtn, 0, Qt::AlignVCenter);

    parent->addLayout(m_buttonRow);

    connect(m_playBtn, &QToolButton::clicked, this, &ControlBar::playPauseClicked);
    connect(m_stopBtn, &QToolButton::clicked, this, &ControlBar::stopClicked);
    connect(m_prevBtn, &QToolButton::clicked, this, &ControlBar::rewindClicked);
    connect(m_nextBtn, &QToolButton::clicked, this, &ControlBar::forwardClicked);
    connect(m_openBtn, &QToolButton::clicked, this, &ControlBar::openFileClicked);
    connect(m_listBtn, &QToolButton::clicked, this, &ControlBar::listClicked);
}

void ControlBar::addToButtonRow(QWidget *w, int index)
{
    if (!m_buttonRow) return;
    if (index < 0)
        m_buttonRow->addWidget(w, 0, Qt::AlignVCenter);
    else
        m_buttonRow->insertWidget(index, w, 0, Qt::AlignVCenter);
    w->setParent(this);
}

void ControlBar::setPlaying(bool playing)
{
    m_playBtn->setText(playing ? "⏸" : "▶");
    m_playBtn->setToolTip(playing ? tr("暂停") : tr("播放"));
}

void ControlBar::setFileLoaded(bool loaded)
{
    m_playBtn->setEnabled(loaded);
    m_stopBtn->setEnabled(loaded);
    m_prevBtn->setEnabled(loaded);
    m_nextBtn->setEnabled(loaded);
}

void ControlBar::setPosition(double pos, double dur)
{
    if (dur > 0) {
        m_seekSlider->setValue(static_cast<int>((pos / dur) * 1000));
    }
}

void ControlBar::setDuration(double dur)
{
    if (dur > 0)
        m_timeLabel->setText("00:00 / " + QString::number(dur, 'f', 0));
    else
        m_timeLabel->setText("00:00 / 00:00");
}

void ControlBar::setVolume(int vol)
{
    m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(vol);
    m_volumeSlider->blockSignals(false);
}

void ControlBar::setMuted(bool muted)
{
    m_volBtn->setText(muted ? "🔇" : "🔊");
}

void ControlBar::setTimeText(const QString &text)
{
    m_timeLabel->setText(text);
}

void ControlBar::setCodecText(const QString &text)
{
    m_codecLabel->setText(text);
}
