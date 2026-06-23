#pragma once

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>

class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setData(const QVector<double> &mags, const QVector<double> &peaks);
    void setTime(double current, double total);
    void setTitle(const QString &title);
    void setBottomReserve(int pixels) { m_bottomReserve = pixels; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void drawTimeSection(QPainter &p, const QRect &rect);
    void drawTitleSection(QPainter &p, const QRect &rect);
    void drawSpectrumSection(QPainter &p, const QRect &rect);
    void restartMarquee();

    QVector<double> m_mags;
    QVector<double> m_peaks;
    int m_bottomReserve = 0;
    static constexpr int kBarCount = 32;
    static constexpr int kTimeSectionWidth = 150;

    double m_currentTime = 0;
    double m_totalTime = 0;
    QString m_title;

    QTimer *m_marqueeTimer = nullptr;
    qreal m_marqueeOffset = 0;
    qreal m_marqueeTextWidth = 0;
    bool m_marqueeNeeded = false;
};
