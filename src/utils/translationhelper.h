#pragma once

#include <QString>
#include <QPair>
#include <QList>

class TranslationHelper
{
public:
    static void initTranslation();
    static void setLanguage(const QString& langCode);
    static QString currentLanguage();
    static QList<QPair<QString, QString>> availableLanguages();
};
