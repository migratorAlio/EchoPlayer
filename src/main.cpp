#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QTranslator>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
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

    const QString serverName = QStringLiteral("EchoPlayer-single-instance");
    QStringList startupFiles;
    for (int i = 1; i < argc; ++i)
        startupFiles << QFile::decodeName(argv[i]);

    // A second launch simply hands its files to the running window.
    {
        QLocalSocket probe;
        probe.connectToServer(serverName, QLocalSocket::WriteOnly);
        if (probe.waitForConnected(200)) {
            QByteArray payload;
            for (const QString &f : startupFiles) {
                payload += f.toUtf8();
                payload += '\n';
            }
            if (!payload.isEmpty()) {
                probe.write(payload);
                probe.waitForBytesWritten(500);
            }
            probe.disconnectFromServer();
            return 0;
        }
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName))
        qWarning("EchoPlayer: local socket unavailable: %s", qPrintable(server.errorString()));

    LanguageManager::instance().switchLanguage("zh_CN");

    MainWindow window;
    window.show();

    QObject::connect(&server, &QLocalServer::newConnection, [&server, &window] {
        while (server.hasPendingConnections()) {
            QLocalSocket *client = server.nextPendingConnection();
            QObject::connect(client, &QLocalSocket::readyRead, [client, &window] {
                const QByteArray data = client->readAll();
                QStringList files;
                for (const QByteArray &line : data.split('\n')) {
                    if (!line.isEmpty())
                        files << QString::fromUtf8(line);
                }
                if (!files.isEmpty())
                    window.activateWithFiles(files);
                client->disconnectFromServer();
            });
            QObject::connect(client, &QLocalSocket::disconnected,
                             client, &QObject::deleteLater);
        }
    });

    if (!startupFiles.isEmpty()) {
        QMetaObject::invokeMethod(qApp, [&window, startupFiles]() {
            window.openMedia(startupFiles.first());
        }, Qt::QueuedConnection);
    }

    return app.exec();
}
