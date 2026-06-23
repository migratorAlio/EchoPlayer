#include "ui/titlebar.h"
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QActionGroup>

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("titleBar");
    setFixedHeight(32);

    setStyleSheet(
        "QWidget#titleBar { background: #1a1a2e; border: none; }"
        "QToolButton { color: #ccc; font-size: 13px; border: none; padding: 4px 8px; }"
        "QToolButton:hover { background: #2d2d5e; }"
        "QToolButton#closeBtn:hover { background: #e81123; color: white; }"
        "QLabel { color: #ccc; font-size: 13px; }"
        "QPushButton#fileMenuBtn { color: #ccc; font-size: 13px; border: none; padding: 4px 8px; }"
        "QPushButton#fileMenuBtn:hover { background: #2d2d5e; }");

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 0, 0, 0);
    lay->setSpacing(0);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(":/icon.png").scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_iconLabel->setFixedSize(24, 32);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("background: transparent;");

    m_fileMenu = new QMenu(this);

    m_openAct = m_fileMenu->addAction(QString());
    m_openAct->setShortcut(QKeySequence::Open);
    connect(m_openAct, &QAction::triggered, this, &TitleBar::openFileRequested);

    m_fileMenu->addSeparator();

    // 配置 → 语言 submenu
    m_configMenu = m_fileMenu->addMenu(QString());
    m_langMenu = m_configMenu->addMenu(QString());

    m_langGroup = new QActionGroup(this);
    m_langGroup->setExclusive(true);

    m_langZhCn = m_langMenu->addAction(QString());
    m_langZhCn->setData("zh_CN");
    m_langZhCn->setCheckable(true);
    m_langZhCn->setChecked(true);
    m_langGroup->addAction(m_langZhCn);

    m_langZhTw = m_langMenu->addAction(QString());
    m_langZhTw->setData("zh_TW");
    m_langZhTw->setCheckable(true);
    m_langGroup->addAction(m_langZhTw);

    m_langEn = m_langMenu->addAction(QString());
    m_langEn->setData("en");
    m_langEn->setCheckable(true);
    m_langGroup->addAction(m_langEn);

    connect(m_langGroup, &QActionGroup::triggered, this, [this](QAction *act) {
        emit languageSwitchRequested(act->data().toString());
    });

    m_fileMenu->addSeparator();

    m_quitAct = m_fileMenu->addAction(QString());
    connect(m_quitAct, &QAction::triggered, qApp, &QApplication::quit);

    // "EchoPlayer ⏷" button with menu
    m_fileMenuBtn = new QPushButton("EchoPlayer ⏷", this);
    m_fileMenuBtn->setObjectName("fileMenuBtn");
    m_fileMenuBtn->setFlat(true);
    m_fileMenuBtn->setCursor(Qt::PointingHandCursor);
    connect(m_fileMenuBtn, &QPushButton::clicked, this, [this]() {
        m_fileMenu->exec(m_fileMenuBtn->mapToGlobal(
            QPoint(0, m_fileMenuBtn->height())));
    });

    m_fileLabel = new QLabel("EchoPlayer", this);

    lay->addWidget(m_iconLabel);
    lay->addWidget(m_fileMenuBtn);
    lay->addWidget(m_fileLabel);
    lay->addStretch();

    m_minBtn = new QToolButton(this);
    m_minBtn->setText("─");

    m_maxBtn = new QToolButton(this);
    updateMaximizeIcon(false);

    m_fullBtn = new QToolButton(this);
    m_fullBtn->setText("⛶");

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setObjectName("closeBtn");
    m_closeBtn->setText("✕");

    lay->addWidget(m_minBtn);
    lay->addWidget(m_maxBtn);
    lay->addWidget(m_fullBtn);
    lay->addWidget(m_closeBtn);

    connect(m_minBtn, &QToolButton::clicked, this, [this]() {
        if (auto *w = window())
            w->showMinimized();
    });
    connect(m_maxBtn, &QToolButton::clicked, this, [this]() {
        if (auto *w = window()) {
            if (w->isMaximized())
                w->showNormal();
            else
                w->showMaximized();
        }
    });
    connect(m_fullBtn, &QToolButton::clicked, this, &TitleBar::fullscreenRequested);
    connect(m_closeBtn, &QToolButton::clicked, qApp, &QApplication::quit);

    retranslateUi();
}

void TitleBar::retranslateUi()
{
    m_openAct->setText(tr("打开..."));
    m_quitAct->setText(tr("退出"));
    m_configMenu->setTitle(tr("配置"));
    m_langMenu->setTitle(tr("语言"));
    m_langZhCn->setText(tr("中文(简体)"));
    m_langZhTw->setText(tr("中文(繁體)"));
    m_langEn->setText(tr("English"));
    m_minBtn->setToolTip(tr("最小化"));
    m_maxBtn->setToolTip(tr("最大化"));
    m_fullBtn->setToolTip(tr("全屏"));
    m_closeBtn->setToolTip(tr("关闭"));
}

void TitleBar::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void TitleBar::setFileName(const QString &name)
{
    m_fileLabel->setText(name.isEmpty() ? "EchoPlayer" : name);
}

void TitleBar::updateMaximizeIcon(bool maximized)
{
    m_maxBtn->setText(maximized ? "❐" : "□");
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = event->globalPosition().toPoint();
        event->accept();
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (auto *w = window()) {
            w->move(w->pos() + event->globalPosition().toPoint() - m_dragStart);
            m_dragStart = event->globalPosition().toPoint();
        }
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (auto *w = window()) {
            if (w->isMaximized())
                w->showNormal();
            else
                w->showMaximized();
        }
        event->accept();
    }
    QWidget::mouseDoubleClickEvent(event);
}
