#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QSocketNotifier>
#include <QPoint>

class PlaylistApp : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistApp(QWidget *parent = nullptr);

    void setPlaylist(const QStringList &filePaths, const QStringList &displayNames);
    void highlightCurrent(const QString &filePath);
    void clearPlaylist();

signals:
    void fileSelected(const QString &filePath);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onStdinActivated(int fd);

private:
    void setupUI();
    void handleLine(const QString &line);

    QListWidget *m_list = nullptr;
    QLabel *m_titleLabel = nullptr;
    QSocketNotifier *m_stdinNotifier = nullptr;

    struct Item {
        QString path;
        QString name;
    };
    QList<Item> m_items;
    QByteArray m_readBuf;

    // Title bar dragging
    QPoint m_dragStart;
    bool m_dragging = false;
};
