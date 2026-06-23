#include "ui/playlistpanel.h"
#include <QFileInfo>

PlaylistPanel::PlaylistPanel(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(260);
    setStyleSheet(
        "PlaylistPanel { background: #1a1a2e; border-left: 1px solid #333; }"
        "QLabel { color: #ccc; font-size: 13px; padding: 8px; }"
        "QListWidget { background: transparent; color: #ccc; font-size: 12px; border: none; outline: none; }"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:hover { background: #2d2d5e; }"
        "QListWidget::item:selected { background: #1a9fff; color: white; }");

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_titleLabel = new QLabel(tr("列表"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_list = new QListWidget(this);
    m_list->setFocusPolicy(Qt::NoFocus);

    lay->addWidget(m_titleLabel);
    lay->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            emit fileSelected(path);
    });
}

void PlaylistPanel::setPlaylist(const QStringList &filePaths, const QStringList &displayNames)
{
    m_list->clear();
    for (int i = 0; i < filePaths.size(); ++i) {
        auto *item = new QListWidgetItem(displayNames[i], m_list);
        item->setData(Qt::UserRole, filePaths[i]);
    }
}

void PlaylistPanel::clear()
{
    m_list->clear();
}
