#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QPoint>

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setFileName(const QString &name);
    void updateMaximizeIcon(bool maximized);

signals:
    void openFileRequested();
    void fullscreenRequested();
    void languageSwitchRequested(const QString &locale);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();

    QLabel *m_fileLabel = nullptr;
    QPushButton *m_fileMenuBtn = nullptr;
    QLabel *m_iconLabel = nullptr;
    QMenu *m_fileMenu = nullptr;
    QToolButton *m_minBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_fullBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_quitAct = nullptr;
    QMenu *m_configMenu = nullptr;
    QMenu *m_langMenu = nullptr;
    QAction *m_langZhCn = nullptr;
    QAction *m_langZhTw = nullptr;
    QAction *m_langEn = nullptr;

    QActionGroup *m_langGroup = nullptr;

    bool m_dragging = false;
    QPoint m_dragStart;
};
