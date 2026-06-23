#include "ui/spectrumwidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(100);
    m_mags.resize(kBarCount);
    m_peaks.resize(kBarCount);
    for (int i = 0; i < kBarCount; ++i) {
        m_mags[i] = 0.0;
        m_peaks[i] = 0.0;
    }

    m_marqueeTimer = new QTimer(this);
    m_marqueeTimer->setInterval(30);
    connect(m_marqueeTimer, &QTimer::timeout, this, [this] {
        int centerW = static_cast<int>((width() - kTimeSectionWidth) * 0.45);
        m_marqueeOffset -= 1.5;
        if (m_marqueeOffset < -m_marqueeTextWidth)
            m_marqueeOffset = centerW;
        update();
    });
}

void SpectrumWidget::setData(const QVector<double> &mags, const QVector<double> &peaks)
{
    m_mags = mags;
    m_peaks = peaks;
    update();
}

void SpectrumWidget::setTime(double current, double total)
{
    m_currentTime = current;
    m_totalTime = total;
    update();
}

void SpectrumWidget::setTitle(const QString &title)
{
    m_title = title;
    restartMarquee();
    update();
}

void SpectrumWidget::restartMarquee()
{
    QFont font = this->font();
    font.setPixelSize(15);
    QFontMetrics fm(font);
    qreal titleW = fm.horizontalAdvance(m_title);
    qreal availW = width() - kTimeSectionWidth;
    qreal centerW = availW * 0.45;
    m_marqueeTextWidth = titleW;
    m_marqueeNeeded = titleW > centerW;

    if (m_marqueeNeeded) {
        m_marqueeOffset = centerW;
        m_marqueeTimer->start();
    } else {
        m_marqueeTimer->stop();
        m_marqueeOffset = 0;
    }
}

void SpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), QColor(0x39, 0x39, 0x39));

    int w = this->width();
    int h = this->height();
    int visibleH = h - m_bottomReserve;
    if (visibleH < 10) visibleH = h;

    // Three-section layout
    int timeW = kTimeSectionWidth;
    int avail = w - timeW;
    int centerW = static_cast<int>(avail * 0.45);
    int specW = avail - centerW;

    QRect timeRect(0, 0, timeW, visibleH);
    QRect titleRect(timeW, 0, centerW, visibleH);
    QRect specRect(timeW + centerW, 0, specW, visibleH);

    // Separators
    p.setPen(QColor(0x33, 0x33, 0x55));
    p.drawLine(timeRect.right(), 8, timeRect.right(), visibleH - 8);
    p.drawLine(titleRect.right(), 8, titleRect.right(), visibleH - 8);

    drawTimeSection(p, timeRect);
    drawTitleSection(p, titleRect);
    drawSpectrumSection(p, specRect);
}

void SpectrumWidget::drawTimeSection(QPainter &p, const QRect &rect)
{
    p.save();
    p.setClipRect(rect);

    auto fmtSec = [](double sec) -> QString {
        if (sec < 0) sec = 0;
        int total = static_cast<int>(std::round(sec));
        int m = total / 60;
        int s = total % 60;
        return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    };

    // Current time (top half) — 25px bold
    {
        QFont curFont = this->font();
        curFont.setPixelSize(25);
        curFont.setBold(true);
        p.setFont(curFont);

        int centerY = rect.center().y();
        int topY = centerY - 22;

        QString curStr = fmtSec(m_currentTime);
        QFontMetrics fm(curFont);
        int curW = fm.horizontalAdvance(curStr);
        int curX = rect.x() + (rect.width() - curW) / 2;
        p.setPen(QColor(0xff, 0xff, 0xff));
        p.drawText(curX, topY, curW, fm.height(), Qt::AlignCenter, curStr);
    }

    // Total time (bottom half) — 16px bold
    {
        QFont totalFont = this->font();
        totalFont.setPixelSize(16);
        totalFont.setBold(true);
        p.setFont(totalFont);

        int centerY = rect.center().y();
        int botY = centerY + 10;

        QString totalStr = fmtSec(m_totalTime);
        QFontMetrics fm(totalFont);
        int totalW = fm.horizontalAdvance(totalStr);
        int totalX = rect.x() + (rect.width() - totalW) / 2;
        p.setPen(QColor(0xff, 0xff, 0xff));
        p.drawText(totalX, botY, totalW, fm.height(), Qt::AlignCenter, totalStr);
    }

    p.restore();
}

void SpectrumWidget::drawTitleSection(QPainter &p, const QRect &rect)
{
    p.save();
    p.setClipRect(rect);

    if (m_title.isEmpty()) {
        p.restore();
        return;
    }

    QFont titleFont = this->font();
    titleFont.setPixelSize(15);
    p.setFont(titleFont);
    p.setPen(QColor(0xff, 0xff, 0xff));

    QFontMetrics fm(titleFont);
    int textH = fm.height();

    if (m_marqueeNeeded) {
        QRect textRect(static_cast<int>(rect.x() + m_marqueeOffset), rect.y(),
                       static_cast<int>(m_marqueeTextWidth), rect.height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);
    } else {
        p.drawText(rect, Qt::AlignCenter, m_title);
    }

    p.restore();
}

void SpectrumWidget::drawSpectrumSection(QPainter &p, const QRect &rect)
{
    p.save();
    p.setClipRect(rect);

    int sw = rect.width();
    int sh = rect.height();
    int startX = rect.x();

    double barGap = 3.0;
    double barTotal = (double)sw / kBarCount;
    double barW = std::max(2.0, barTotal - barGap);
    double baseY = rect.y() + sh - 8;

    double segH = 4.0;
    double segGap = 1.5;
    double segUnit = segH + segGap;

    for (int b = 0; b < kBarCount && b < m_mags.size(); ++b) {
        double raw = std::max(0.0, std::min(1.0, m_mags[b]));
        double mag = std::min(1.0, raw * 1.3);
        double barH = std::max(1.0, mag * (sh - 16));
        double x = startX + b * barTotal + (barTotal - barW) / 2.0;

        // Blue gradient: darker at bottom, brighter at top
        double t = mag;
        int r = 0x22 + static_cast<int>(t * (0x44 - 0x22));
        int g = 0x66 + static_cast<int>(t * (0xbb - 0x66));
        int bv = 0xcc + static_cast<int>(t * (0xff - 0xcc));

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(r, g, bv));

        // Segmented bar: draw short blocks from bottom up
        int segmentCount = static_cast<int>(barH / segUnit);
        for (int s = 0; s < segmentCount; ++s) {
            double segY = baseY - (s + 1) * segUnit + segGap;
            p.drawRoundedRect(QRectF(x, segY, barW, segH), 1.0, 1.0);
        }
        // Draw partial remainder segment if it's large enough
        double remainder = barH - segmentCount * segUnit;
        if (remainder > segH * 0.5) {
            double segY = baseY - segmentCount * segUnit - remainder;
            p.drawRoundedRect(QRectF(x, segY, barW, remainder), 1.0, 1.0);
        }

        // Peak dot
        if (b < m_peaks.size() && m_peaks[b] > 0.01) {
            double peakY = baseY - std::max(0.0, std::min(1.0, m_peaks[b])) * (sh - 16);
            p.setBrush(QColor(0x88, 0xcc, 0xff));
            p.drawEllipse(QPointF(x + barW / 2.0, peakY), 2.0, 2.0);
        }
    }

    p.restore();
}

void SpectrumWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    restartMarquee();
}
