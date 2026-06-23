#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QTranslator>
#include <QFile>
#include <clocale>
#include "ui/mainwindow.h"
#include "i18n/languagemanager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    app.setApplicationName("EchoPlayer");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/icon.png"));

    // Install initial translator (default: zh_CN)
    auto &langMgr = LanguageManager::instance();
    langMgr.switchLanguage("zh_CN");

    MainWindow window;
    window.show();

    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        QString file = QFile::decodeName(argv[1]);
        QMetaObject::invokeMethod(qApp, [&window, file]() {
            window.openMedia(file);
        }, Qt::QueuedConnection);
    }

    return app.exec();
}
