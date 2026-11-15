#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QSocketNotifier>
#include <QPoint>
#include <QStringList>

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
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onStdinActivated(int fd);
    void onAddFiles();
    void onClear();
    void onToggleShuffle();
    void onCycleRepeat();

private:
    void setupUI();
    void retranslateUi();
    void handleLine(const QString &line);
    void sendCommand(const QString &name, const QStringList &args = {});
    void updateModeUi();

    QListWidget *m_list = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QSocketNotifier *m_stdinNotifier = nullptr;
    QToolButton *m_shuffleBtn = nullptr;
    QToolButton *m_repeatBtn = nullptr;

    struct Item {
        QString path;
        QString name;
    };
    QList<Item> m_items;
    QByteArray m_readBuf;
    QString m_currentPath;
    int m_repeat = 0;
    bool m_shuffle = false;

    // Title bar dragging
    QPoint m_dragStart;
    bool m_dragging = false;
};
