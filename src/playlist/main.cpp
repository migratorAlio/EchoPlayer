#include <QApplication>
#include <QSettings>
#include "playlist/playlistapp.h"
#include "i18n/languagemanager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("EchoPlayer-playlist");
    app.setApplicationVersion("1.0.0");

    // Same QSettings scope as the player, so the playlist window opens in the
    // language the user picked even before the first LANG message arrives.
    QSettings settings(QStringLiteral("EchoPlayer"), QStringLiteral("EchoPlayer"));
    LanguageManager::instance().switchLanguage(
        settings.value(QStringLiteral("language"), QStringLiteral("zh_CN")).toString());

    PlaylistApp playlist;
    playlist.show();

    return app.exec();
}
