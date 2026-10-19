#include "translationhelper.h"
#include "confighandler.h"
#include "pathinfo.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>
#include <QLocale>
#include <QWidget>
#include <QEvent>
#include <QDir>
#include <QFileInfo>

static QTranslator s_appTranslator;
static QTranslator s_qtTranslator;

QList<QPair<QString, QString>> TranslationHelper::availableLanguages()
{
    static QList<QPair<QString, QString>> list = {
        {QString(), QStringLiteral("System (Auto) / Системный")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ru"), QStringLiteral("Русский (Russian)")},
        {QStringLiteral("uk"), QStringLiteral("Українська (Ukrainian)")},
        {QStringLiteral("de_DE"), QStringLiteral("Deutsch (German)")},
        {QStringLiteral("fr"), QStringLiteral("Français (French)")},
        {QStringLiteral("es"), QStringLiteral("Español (Spanish)")},
        {QStringLiteral("it_IT"), QStringLiteral("Italiano (Italian)")},
        {QStringLiteral("pl"), QStringLiteral("Polski (Polish)")},
        {QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)")},
        {QStringLiteral("zh_CN"), QStringLiteral("中文 (简体) (Chinese)")},
        {QStringLiteral("zh_TW"), QStringLiteral("中文 (繁體) (Traditional Chinese)")},
        {QStringLiteral("zh_HK"), QStringLiteral("中文 (香港) (Hong Kong)")},
        {QStringLiteral("ja"), QStringLiteral("日本語 (Japanese)")},
        {QStringLiteral("ko"), QStringLiteral("한국어 (Korean)")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe (Turkish)")},
        {QStringLiteral("cs"), QStringLiteral("Čeština (Czech)")},
        {QStringLiteral("sk"), QStringLiteral("Slovenčina (Slovak)")},
        {QStringLiteral("hu"), QStringLiteral("Magyar (Hungarian)")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands (Dutch)")},
        {QStringLiteral("nl_NL"), QStringLiteral("Nederlands (Nederland)")},
        {QStringLiteral("da"), QStringLiteral("Dansk (Danish)")},
        {QStringLiteral("fi"), QStringLiteral("Suomi (Finnish)")},
        {QStringLiteral("sv_SE"), QStringLiteral("Svenska (Swedish)")},
        {QStringLiteral("nb_NO"), QStringLiteral("Norsk Bokmål (Norwegian)")},
        {QStringLiteral("el"), QStringLiteral("Ελληνικά (Greek)")},
        {QStringLiteral("bg"), QStringLiteral("Български (Bulgarian)")},
        {QStringLiteral("sr_SP"), QStringLiteral("Српски (Serbian)")},
        {QStringLiteral("he"), QStringLiteral("עברית (Hebrew)")},
        {QStringLiteral("fa"), QStringLiteral("فارسی (Persian)")},
        {QStringLiteral("vi"), QStringLiteral("Tiếng Việt (Vietnamese)")},
        {QStringLiteral("id"), QStringLiteral("Bahasa Indonesia (Indonesian)")},
        {QStringLiteral("ca"), QStringLiteral("Català (Catalan)")},
        {QStringLiteral("eu"), QStringLiteral("Euskara (Basque)")},
        {QStringLiteral("gl"), QStringLiteral("Galego (Galician)")},
        {QStringLiteral("ka"), QStringLiteral("ქართული (Georgian)")},
        {QStringLiteral("grc"), QStringLiteral("Ἀρχαία ἑλληνική (Ancient Greek)")}
    };
    return list;
}

QString TranslationHelper::currentLanguage()
{
    return ConfigHandler().uiLanguage();
}

void TranslationHelper::setLanguage(const QString& langCode)
{
    ConfigHandler().setUiLanguage(langCode);

    qApp->removeTranslator(&s_appTranslator);
    qApp->removeTranslator(&s_qtTranslator);

    bool isEnglish = (langCode == QStringLiteral("en") || langCode == QStringLiteral("en_US"));
    QLocale targetLocale;
    if (langCode.isEmpty() || langCode == QStringLiteral("system")) {
        targetLocale = QLocale::system();
        isEnglish = (targetLocale.language() == QLocale::English);
    } else {
        targetLocale = QLocale(langCode);
    }

    if (!isEnglish) {
        QStringList trPaths = PathInfo::translationsPaths();
        trPaths.prepend(QCoreApplication::applicationDirPath() + QStringLiteral("/translations"));
        trPaths.append(QStringLiteral("/home/user/test/screenink-alternative/translations"));
        trPaths.append(QStringLiteral("/usr/share/flameshot/translations"));

        for (const QString& path : trPaths) {
            if (s_appTranslator.load(targetLocale, QStringLiteral("Internationalization"), QStringLiteral("_"), path)) {
                qApp->installTranslator(&s_appTranslator);
                break;
            }
        }
        for (const QString& path : trPaths) {
            if (s_qtTranslator.load(targetLocale, QStringLiteral("qt"), QStringLiteral("_"), path)) {
                qApp->installTranslator(&s_qtTranslator);
                break;
            }
        }
    }

    for (QWidget* w : qApp->topLevelWidgets()) {
        QEvent ev(QEvent::LanguageChange);
        QCoreApplication::sendEvent(w, &ev);
    }
}

void TranslationHelper::initTranslation()
{
    QString savedLang = ConfigHandler().uiLanguage();
    setLanguage(savedLang);
}
