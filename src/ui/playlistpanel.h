#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>

class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(QWidget *parent = nullptr);

    void setPlaylist(const QStringList &filePaths, const QStringList &displayNames);
    void clear();
    int count() const { return m_list->count(); }

signals:
    void fileSelected(const QString &filePath);

private:
    QListWidget *m_list = nullptr;
    QLabel *m_titleLabel = nullptr;
};
