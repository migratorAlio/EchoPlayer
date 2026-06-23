#pragma once

#include <QObject>
#include <QTranslator>
#include <QMap>
#include <QPair>
#include <QString>

// QTranslator that loads .ts (XML) files directly — no lrelease needed
class TsTranslator : public QTranslator {
    Q_OBJECT
public:
    explicit TsTranslator(QObject *parent = nullptr) : QTranslator(parent) {}
    bool load(const QString &tsFile);
    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override;

private:
    QMap<QPair<QString, QString>, QString> m_strings;
};

class LanguageManager : public QObject {
    Q_OBJECT
public:
    static LanguageManager &instance();

    void switchLanguage(const QString &locale);
    QString currentLocale() const { return m_currentLocale; }

    static QString translationsDir();

signals:
    void languageChanged();

private:
    LanguageManager();
    ~LanguageManager() override = default;

    TsTranslator *m_translator = nullptr;
    QString m_currentLocale;
};
