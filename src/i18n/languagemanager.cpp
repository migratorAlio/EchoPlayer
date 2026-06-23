#include "i18n/languagemanager.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QXmlStreamReader>

// --- TsTranslator ---

static QString unescapeTs(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('\\') && i + 1 < s.size()) {
            switch (s[i + 1].toLatin1()) {
            case 'n': out += QLatin1Char('\n'); ++i; break;
            case 't': out += QLatin1Char('\t'); ++i; break;
            case '\\': out += QLatin1Char('\\'); ++i; break;
            case '"': out += QLatin1Char('"'); ++i; break;
            default: out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

bool TsTranslator::load(const QString &tsFile)
{
    QFile file(tsFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QXmlStreamReader xml(&file);
    m_strings.clear();

    QString currentContext;
    while (!xml.atEnd() && !xml.hasError()) {
        auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            if (xml.name() == QStringLiteral("context")) {
                currentContext.clear();
            } else if (xml.name() == QStringLiteral("name")) {
                currentContext = xml.readElementText();
            } else if (xml.name() == QStringLiteral("message")) {
                QString source, translation;
                while (!xml.atEnd()) {
                    auto t = xml.readNext();
                    if (t == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("message"))
                        break;
                    if (t == QXmlStreamReader::StartElement) {
                        if (xml.name() == QStringLiteral("source"))
                            source = xml.readElementText();
                        else if (xml.name() == QStringLiteral("translation"))
                            translation = xml.readElementText();
                    }
                }
                if (!currentContext.isEmpty() && !source.isEmpty()) {
                    auto key = qMakePair(unescapeTs(currentContext), unescapeTs(source));
                    m_strings[key] = unescapeTs(translation);
                }
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "[TsTranslator] XML parse error:" << xml.errorString();
        return false;
    }

    qDebug() << "[TsTranslator] loaded" << m_strings.size() << "strings from" << tsFile;
    return true;
}

QString TsTranslator::translate(const char *context, const char *sourceText,
                                 const char *disambiguation, int n) const
{
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)
    auto it = m_strings.find(qMakePair(QString::fromUtf8(context), QString::fromUtf8(sourceText)));
    if (it != m_strings.end() && !it.value().isEmpty())
        return it.value();
    return QString();
}

// --- LanguageManager ---

LanguageManager &LanguageManager::instance()
{
    static LanguageManager inst;
    return inst;
}

LanguageManager::LanguageManager()
    : QObject(nullptr)
{
    m_translator = new TsTranslator(this);
}

QString LanguageManager::translationsDir()
{
    QStringList candidates = {
        QApplication::applicationDirPath() + "/translations",
        QApplication::applicationDirPath() + "/../src/i18n",
        QDir::currentPath() + "/translations",
        QDir::currentPath() + "/src/i18n",
    };
    for (const auto &dir : candidates) {
        if (QDir(dir).exists())
            return QDir(dir).absolutePath();
    }
    return QApplication::applicationDirPath() + "/translations";
}

void LanguageManager::switchLanguage(const QString &locale)
{
    if (m_currentLocale == locale)
        return;

    QString tsFile = translationsDir() + "/EchoPlayer_" + locale + ".ts";

    if (!QFile::exists(tsFile)) {
        qWarning() << "[i18n] .ts not found:" << tsFile;
        return;
    }

    qApp->removeTranslator(m_translator);

    if (m_translator->load(tsFile)) {
        qApp->installTranslator(m_translator);
        m_currentLocale = locale;
        qDebug() << "[i18n] switched to" << locale;
        emit languageChanged();
    } else {
        qWarning() << "[i18n] failed to load" << tsFile;
    }
}
