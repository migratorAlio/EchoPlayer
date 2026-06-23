#pragma once

#include <QString>
#include <QStringList>
#include <QRect>

namespace PlaylistProtocol {

inline QString makePlaylist(const QStringList &paths, const QStringList &names = {})
{
    if (names.isEmpty())
        return "PLAYLIST\t" + paths.join('\t');

    QStringList parts;
    for (int i = 0; i < paths.size(); ++i) {
        parts << paths[i];
        parts << (i < names.size() ? names[i] : QString());
    }
    return "PLAYLIST\t" + parts.join('\t');
}

inline bool parsePlaylist(const QString &data, QStringList &paths, QStringList &names, bool interleaved = true)
{
    QStringList parts = data.split('\t', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;

    paths.clear();
    names.clear();

    if (interleaved) {
        // Format: path1,name1,path2,name2,...
        for (int i = 0; i + 1 < parts.size(); i += 2) {
            paths << parts[i];
            names << parts[i + 1];
        }
        if (parts.size() % 2 == 1)
            paths << parts.last();
    } else {
        paths = parts;
    }
    return !paths.isEmpty();
}

inline QString makeSelect(const QString &path)  { return "SELECT\t" + path; }
inline QString makeCurrent(const QString &path) { return "CURRENT\t" + path; }

inline QString makeGeometry(int x, int y, int w, int h)
{
    return QString("GEOMETRY\t%1\t%2\t%3\t%4").arg(x).arg(y).arg(w).arg(h);
}

inline QRect parseGeometry(const QString &data)
{
    auto parts = data.split('\t');
    if (parts.size() < 4) return {};
    return QRect(parts[0].toInt(), parts[1].toInt(),
                 parts[2].toInt(), parts[3].toInt());
}

} // namespace PlaylistProtocol
