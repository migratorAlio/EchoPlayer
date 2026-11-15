#pragma once

#include <QString>
#include <QList>

struct AudioTrackInfo {
    int streamIndex = -1;
    QString language;
    QString displayName;
};

struct LyricLine {
    double timestamp = 0;
    QString text;
};

struct CueTrack {
    int trackNo = 0;
    QString title;
    QString performer;
    double startTime = 0; // seconds
    QString sourceFile;   // absolute path to the audio file
};

inline QString languageName(const QString &code)
{
    if (code == "chi" || code == "zho" || code == "zh") return "Chinese";
    if (code == "eng") return "English";
    if (code == "jpn" || code == "ja") return "Japanese";
    if (code == "kor" || code == "ko") return "Korean";
    if (code == "fre" || code == "fra" || code == "fr") return "French";
    if (code == "ger" || code == "deu" || code == "de") return "German";
    if (code == "spa" || code == "es") return "Spanish";
    if (code == "rus" || code == "ru") return "Russian";
    if (code == "ita" || code == "it") return "Italian";
    if (code == "tha" || code == "th") return "Thai";
    if (code == "vie" || code == "vi") return "Vietnamese";
    if (code == "ara" || code == "ar") return "Arabic";
    if (code == "por" || code == "pt") return "Portuguese";
    if (code == "dut" || code == "nld" || code == "nl") return "Dutch";
    return code;
}
