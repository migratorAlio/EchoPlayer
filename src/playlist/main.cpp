#include <QApplication>
#include "playlist/playlistapp.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("EchoPlayer-playlist");
    app.setApplicationVersion("1.0.0");

    PlaylistApp playlist;
    playlist.show();

    return app.exec();
}
