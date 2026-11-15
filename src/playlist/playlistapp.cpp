#include "playlist/playlistapp.h"
#include "playlist/playlistprotocol.h"
#include "i18n/languagemanager.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QUrl>

#include <unistd.h>

PlaylistApp::PlaylistApp(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAcceptDrops(true);
    setupUI();

    if (!isatty(STDIN_FILENO)) {
        m_stdinNotifier = new QSocketNotifier(STDIN_FILENO,
            QSocketNotifier::Read, this);
        connect(m_stdinNotifier, &QSocketNotifier::activated,
                this, &PlaylistApp::onStdinActivated);
    }
}

void PlaylistApp::setupUI()
{
    setFixedWidth(280);
    setWindowTitle(tr("Playlist"));

    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // Custom title bar
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName("playlistTitleBar");
    titleBar->setFixedHeight(32);
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(8, 0, 4, 0);

    m_titleLabel = new QLabel(tr("Playlist"), this);
    m_titleLabel->setObjectName("playlistTitleLabel");

    auto *closeBtn = new QToolButton(this);
    closeBtn->setObjectName("playlistCloseBtn");
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setFixedSize(20, 20);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);

    titleLay->addWidget(m_titleLabel);
    titleLay->addStretch();
    titleLay->addWidget(closeBtn);

    m_list = new QListWidget(this);
    m_list->setObjectName("playlistList");
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setDragDropMode(QAbstractItemView::NoDragDrop);

    // Tool row: add / clear / shuffle / repeat
    auto *tools = new QWidget(this);
    tools->setObjectName("playlistTools");
    tools->setFixedHeight(30);
    auto *toolLay = new QHBoxLayout(tools);
    toolLay->setContentsMargins(4, 2, 4, 2);
    toolLay->setSpacing(4);

    auto makeButton = [this, tools](const QString &text) {
        auto *b = new QToolButton(tools);
        b->setText(text);
        b->setFixedHeight(24);
        b->setObjectName("playlistToolBtn");
        return b;
    };
    m_addBtn = makeButton(tr("添加"));
    m_addBtn->setToolTip(tr("添加文件到列表"));
    connect(m_addBtn, &QToolButton::clicked, this, &PlaylistApp::onAddFiles);

    m_clearBtn = makeButton(tr("清空"));
    m_clearBtn->setToolTip(tr("清空列表"));
    connect(m_clearBtn, &QToolButton::clicked, this, &PlaylistApp::onClear);

    m_shuffleBtn = makeButton(tr("随机"));
    m_shuffleBtn->setCheckable(true);
    m_shuffleBtn->setToolTip(tr("随机播放"));
    connect(m_shuffleBtn, &QToolButton::clicked,
            this, &PlaylistApp::onToggleShuffle);

    m_repeatBtn = makeButton(tr("不循环"));
    m_repeatBtn->setToolTip(tr("切换循环模式"));
    connect(m_repeatBtn, &QToolButton::clicked,
            this, &PlaylistApp::onCycleRepeat);

    toolLay->addWidget(m_addBtn);
    toolLay->addWidget(m_clearBtn);
    toolLay->addWidget(m_shuffleBtn);
    toolLay->addWidget(m_repeatBtn, 1);

    mainLay->addWidget(titleBar);
    mainLay->addWidget(m_list, 1);
    mainLay->addWidget(tools);

    setStyleSheet(
        "PlaylistApp { background: #1a1a2e; }"
        "QWidget#playlistTitleBar { background: #1a1a2e; border: none; }"
        "QWidget#playlistTools { background: #141426; border-top: 1px solid #2d2d4e; }"
        "QLabel#playlistTitleLabel { color: #ccc; font-size: 13px; }"
        "QToolButton#playlistCloseBtn { color: #999; background: transparent; border: none; font-size: 13px; }"
        "QToolButton#playlistCloseBtn:hover { color: white; background: #c0392b; border-radius: 2px; }"
        "QToolButton#playlistToolBtn { color: #bbb; background: #23233f; border: none;"
        " border-radius: 3px; font-size: 11px; padding: 0 4px; }"
        "QToolButton#playlistToolBtn:hover { background: #34346a; color: white; }"
        "QToolButton#playlistToolBtn:checked { background: #1a9fff; color: white; }"
        "QListWidget#playlistList { background: transparent; color: #ccc; font-size: 12px; border: none; outline: none; }"
        "QListWidget#playlistList::item { padding: 6px 8px; }"
        "QListWidget#playlistList::item:hover { background: #2d2d5e; }"
        "QListWidget#playlistList::item:selected { background: #1a9fff; color: white; }");

    connect(m_list, &QListWidget::itemClicked, this, &PlaylistApp::onItemClicked);
    updateModeUi();
}

void PlaylistApp::retranslateUi()
{
    setWindowTitle(tr("Playlist"));
    m_titleLabel->setText(tr("Playlist"));
    m_addBtn->setText(tr("添加"));
    m_addBtn->setToolTip(tr("添加文件到列表"));
    m_clearBtn->setText(tr("清空"));
    m_clearBtn->setToolTip(tr("清空列表"));
    m_shuffleBtn->setText(tr("随机"));
    m_shuffleBtn->setToolTip(tr("随机播放"));
    m_repeatBtn->setToolTip(tr("切换循环模式"));
    updateModeUi();
}

void PlaylistApp::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void PlaylistApp::mousePressEvent(QMouseEvent *event)
{
    if (event->position().y() <= 32) {
        m_dragging = true;
        m_dragStart = event->globalPosition().toPoint();
        event->accept();
    }
}

void PlaylistApp::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStart;
        move(pos() + delta);
        m_dragStart = event->globalPosition().toPoint();
        event->accept();
    }
}

void PlaylistApp::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        event->accept();
    }
}

void PlaylistApp::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint inList = m_list->viewport()->mapFromGlobal(event->globalPos());
    const int row = m_list->indexAt(inList).isValid()
        ? m_list->indexAt(inList).row() : m_list->currentRow();

    QMenu menu(this);
    QAction *remove = menu.addAction(tr("从列表移除"));
    remove->setEnabled(row >= 0);
    QAction *up = menu.addAction(tr("上移"));
    up->setEnabled(row > 0);
    QAction *down = menu.addAction(tr("下移"));
    down->setEnabled(row >= 0 && row + 1 < m_list->count());
    menu.addSeparator();
    QAction *clear = menu.addAction(tr("清空列表"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == remove)
        sendCommand(QStringLiteral("REMOVE"), {QString::number(row)});
    else if (chosen == up)
        sendCommand(QStringLiteral("MOVE"),
                    {QString::number(row), QString::number(row - 1)});
    else if (chosen == down)
        sendCommand(QStringLiteral("MOVE"),
                    {QString::number(row), QString::number(row + 1)});
    else if (chosen == clear)
        onClear();
}

void PlaylistApp::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void PlaylistApp::dropEvent(QDropEvent *event)
{
    if (!event->mimeData())
        return;
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString p = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!p.isEmpty())
            paths << p;
    }
    if (!paths.isEmpty())
        sendCommand(QStringLiteral("ADD"), paths);
    event->acceptProposedAction();
}

void PlaylistApp::sendCommand(const QString &name, const QStringList &args)
{
    const QByteArray msg = (PlaylistProtocol::makeCommand(name, args)
                            + QStringLiteral("\n")).toUtf8();
    fwrite(msg.constData(), 1, msg.size(), stdout);
    fflush(stdout);
}

void PlaylistApp::updateModeUi()
{
    if (!m_shuffleBtn || !m_repeatBtn)
        return;
    m_shuffleBtn->setChecked(m_shuffle);
    const QString repeatText = m_repeat == 1 ? tr("列表循环")
        : (m_repeat == 2 ? tr("单曲循环") : tr("不循环"));
    m_repeatBtn->setText(repeatText);
}

void PlaylistApp::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("添加文件到播放列表"), {},
        QStringLiteral("Media files (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.ts "
                       "*.mp3 *.flac *.wav *.aac *.ogg *.m4a);;All files (*)"));
    if (!files.isEmpty())
        sendCommand(QStringLiteral("ADD"), files);
}

void PlaylistApp::onClear()
{
    sendCommand(QStringLiteral("CLEAR"));
}

void PlaylistApp::onToggleShuffle()
{
    m_shuffle = !m_shuffle;
    updateModeUi();
    sendCommand(QStringLiteral("MODE"),
                {QString::number(m_repeat), QString::number(m_shuffle ? 1 : 0)});
}

void PlaylistApp::onCycleRepeat()
{
    m_repeat = (m_repeat + 1) % 3;
    updateModeUi();
    sendCommand(QStringLiteral("MODE"),
                {QString::number(m_repeat), QString::number(m_shuffle ? 1 : 0)});
}

void PlaylistApp::onStdinActivated(int)
{
    char buf[4096];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n <= 0) return;

    m_readBuf.append(buf, n);

    while (true) {
        int nl = m_readBuf.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_readBuf.left(nl).trimmed();
        m_readBuf.remove(0, nl + 1);
        if (!line.isEmpty())
            handleLine(QString::fromUtf8(line));
    }
}

void PlaylistApp::handleLine(const QString &line)
{
    if (line.startsWith("PLAYLIST\t")) {
        const QString data = line.mid(QString("PLAYLIST\t").length());
        if (data.trimmed().isEmpty()) {
            clearPlaylist();
            return;
        }
        QStringList paths, names;
        PlaylistProtocol::parsePlaylist(data, paths, names);
        if (!paths.isEmpty()) {
            if (names.size() != paths.size()) {
                names.clear();
                for (const auto &p : paths) {
                    QString clean = p.section('?', 0, 0);
                    names << QFileInfo(clean).completeBaseName();
                }
            }
            setPlaylist(paths, names);
        }
    } else if (line.startsWith("CURRENT\t")) {
        highlightCurrent(line.mid(QString("CURRENT\t").length()));
    } else if (line.startsWith("MODE\t")) {
        const QStringList parts = line.split('\t');
        if (parts.size() >= 3) {
            m_repeat = qBound(0, parts.at(1).toInt(), 2);
            m_shuffle = parts.at(2).toInt() != 0;
            updateModeUi();
        }
    } else if (line.startsWith("LANG\t")) {
        LanguageManager::instance().switchLanguage(line.mid(5).trimmed());
    } else if (line.startsWith("GEOMETRY\t")) {
        QString data = line.mid(QString("GEOMETRY\t").length());
        QRect r = PlaylistProtocol::parseGeometry(data);
        if (r.isValid()) {
            move(r.right(), r.top());
            setFixedHeight(r.height());
        }
    }
}

void PlaylistApp::setPlaylist(const QStringList &filePaths, const QStringList &displayNames)
{
    m_list->clear();
    m_items.clear();
    for (int i = 0; i < filePaths.size() && i < displayNames.size(); ++i) {
        m_items.append({filePaths[i], displayNames[i]});
        auto *item = new QListWidgetItem(displayNames[i], m_list);
        item->setData(Qt::UserRole, filePaths[i]);
    }
    m_titleLabel->setText(tr("Playlist (%1)").arg(m_list->count()));
    highlightCurrent(m_currentPath);
}

void PlaylistApp::highlightCurrent(const QString &filePath)
{
    m_currentPath = filePath;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->data(Qt::UserRole).toString() == filePath) {
            item->setForeground(QColor("#1a9fff"));
            item->setSelected(true);
        } else {
            item->setForeground(QColor("#ccc"));
        }
    }
}

void PlaylistApp::clearPlaylist()
{
    m_list->clear();
    m_items.clear();
    m_titleLabel->setText(tr("Playlist (0)"));
}

void PlaylistApp::onItemClicked(QListWidgetItem *item)
{
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    emit fileSelected(path);

    QByteArray msg = (PlaylistProtocol::makeSelect(path) + "\n").toUtf8();
    fwrite(msg.constData(), 1, msg.size(), stdout);
    fflush(stdout);
}
