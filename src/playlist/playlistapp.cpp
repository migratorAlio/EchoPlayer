#include "playlist/playlistapp.h"
#include "playlist/playlistprotocol.h"
#include <QApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QScreen>
#include <QMouseEvent>
#include <unistd.h>

PlaylistApp::PlaylistApp(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
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
    setFixedWidth(260);
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
    closeBtn->setText("✕");
    closeBtn->setFixedSize(20, 20);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);

    titleLay->addWidget(m_titleLabel);
    titleLay->addStretch();
    titleLay->addWidget(closeBtn);

    m_list = new QListWidget(this);
    m_list->setObjectName("playlistList");
    m_list->setFocusPolicy(Qt::NoFocus);

    mainLay->addWidget(titleBar);
    mainLay->addWidget(m_list, 1);

    setStyleSheet(
        "PlaylistApp { background: #1a1a2e; }"
        "QWidget#playlistTitleBar { background: #1a1a2e; border: none; }"
        "QLabel#playlistTitleLabel { color: #ccc; font-size: 13px; }"
        "QToolButton#playlistCloseBtn { color: #999; background: transparent; border: none; font-size: 13px; }"
        "QToolButton#playlistCloseBtn:hover { color: white; background: #c0392b; border-radius: 2px; }"
        "QListWidget#playlistList { background: transparent; color: #ccc; font-size: 12px; border: none; outline: none; }"
        "QListWidget#playlistList::item { padding: 6px 8px; }"
        "QListWidget#playlistList::item:hover { background: #2d2d5e; }"
        "QListWidget#playlistList::item:selected { background: #1a9fff; color: white; }");

    connect(m_list, &QListWidget::itemClicked, this, &PlaylistApp::onItemClicked);
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
        QString data = line.mid(QString("PLAYLIST\t").length());
        QStringList paths, names;
        PlaylistProtocol::parsePlaylist(data, paths, names);
        if (!paths.isEmpty()) {
            if (names.size() != paths.size()) {
                // Fallback: derive names from paths
                names.clear();
                for (const auto &p : paths) {
                    QString clean = p.section('?', 0, 0);
                    names << QFileInfo(clean).completeBaseName();
                }
            }
            setPlaylist(paths, names);
        }
    } else if (line.startsWith("CURRENT\t")) {
        QString path = line.mid(QString("CURRENT\t").length());
        highlightCurrent(path);
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
}

void PlaylistApp::highlightCurrent(const QString &filePath)
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->data(Qt::UserRole).toString() == filePath) {
            item->setForeground(QColor("#1a9fff"));
        } else {
            item->setForeground(QColor("#ccc"));
        }
    }
}

void PlaylistApp::clearPlaylist()
{
    m_list->clear();
    m_items.clear();
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
