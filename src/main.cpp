// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/Types.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PDF Libre"));
    QApplication::setOrganizationName(QStringLiteral("PDF Libre"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    qRegisterMetaType<OutlineList>();
    qRegisterMetaType<RenderRequest>();

    // Boîtes de dialogue standard de Qt (impression, fichiers) en français.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))
        || qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                             QApplication::applicationDirPath() + QStringLiteral("/translations")))
        QApplication::installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lecteur PDF libre et rapide"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Options réservées aux tests automatisés de l'interface.
    QCommandLineOption captureOption(QStringLiteral("capture"), QStringLiteral("Enregistre une capture de la fenêtre puis quitte"),
                                     QStringLiteral("fichier"));
    QCommandLineOption searchOption(QStringLiteral("rechercher"), QStringLiteral("Lance une recherche dans le document"),
                                    QStringLiteral("texte"));
    captureOption.setFlags(QCommandLineOption::HiddenFromHelp);
    searchOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(captureOption);
    parser.addOption(searchOption);
    parser.addPositionalArgument(QStringLiteral("fichiers"), QStringLiteral("Documents à ouvrir"), QStringLiteral("[fichiers...]"));
    parser.process(app);

    MainWindow window;
    window.show();
    for (const QString &file : parser.positionalArguments())
        window.openFile(file);

    if (parser.isSet(searchOption))
        QTimer::singleShot(600, &window, [&] { window.startSearch(parser.value(searchOption)); });
    if (parser.isSet(captureOption)) {
        QTimer::singleShot(2000, &window, [&] {
            window.grab().save(parser.value(captureOption));
            QApplication::quit();
        });
    }

    return app.exec();
}
