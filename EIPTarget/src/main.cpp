/*******************************************************************************
 * main.cpp - EIP Target application entry point
 *
 * Supports two modes:
 *   GUI mode (default):  EIPTarget
 *   Headless mode:       EIPTarget --headless --iface <index>
 *                                  [--input-size N] [--output-size N]
 ******************************************************************************/

#include <QCoreApplication>
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTextStream>

#include "eiptargetservice.h"
#include "mainwindow.h"

static int runHeadless(QCoreApplication &app,
                       const QString &ifaceIndex,
                       int inputSize, int outputSize)
{
    QTextStream out(stdout);

    EipTargetService svc;
    svc.config().inputSize  = inputSize;
    svc.config().outputSize = outputSize;

    QObject::connect(&svc, &EipTargetService::logMessage,
                     [&out](const QString &msg) { out << msg << "\n"; out.flush(); });
    QObject::connect(&svc, &EipTargetService::errorOccurred,
                     [&out](const QString &err) { out << "ERROR: " << err << "\n"; out.flush(); });
    QObject::connect(&svc, &EipTargetService::outputDataReceived,
                     [&out](const QByteArray &data) {
        out << "Output: " << data.toHex(' ').toUpper() << "\n"; out.flush();
    });
    QObject::connect(&svc, &EipTargetService::started,
                     [&out]() { out << "EIP Target started.\n"; out.flush(); });
    QObject::connect(&svc, &EipTargetService::stopped,
                     [&app]() { app.quit(); });

    svc.start(ifaceIndex);
    return app.exec();
}

int main(int argc, char *argv[])
{
    /* Pre-scan for --headless before constructing Q*Application,
       since QCoreApplication (no GUI) is lighter than QApplication. */
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--headless") == 0) { headless = true; break; }
    }

    if (headless) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("EIPTarget"));

        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("EtherNet/IP Target (Adapter)"));
        parser.addHelpOption();
        parser.addOption({QStringLiteral("headless"),
                          QStringLiteral("Run without GUI")});
        parser.addOption({QStringLiteral("iface"),
                          QStringLiteral("Network interface index"), QStringLiteral("index")});
        parser.addOption({QStringLiteral("input-size"),
                          QStringLiteral("Input assembly size in bytes (1-512, default 32)"),
                          QStringLiteral("bytes"), QStringLiteral("32")});
        parser.addOption({QStringLiteral("output-size"),
                          QStringLiteral("Output assembly size in bytes (1-512, default 32)"),
                          QStringLiteral("bytes"), QStringLiteral("32")});
        parser.addOption({QStringLiteral("list-ifaces"),
                          QStringLiteral("List available network interfaces and exit")});
        parser.process(app);

        if (parser.isSet(QStringLiteral("list-ifaces"))) {
            QTextStream out(stdout);
            const auto ifaces = EipTargetService::availableInterfaces();
            for (const auto &ni : ifaces)
                out << ni.displayLabel << "\n";
            return 0;
        }

        if (!parser.isSet(QStringLiteral("iface"))) {
            QTextStream err(stderr);
            err << "Error: --iface is required in headless mode.\n";
            err << "Use --list-ifaces to see available interfaces.\n";
            return 1;
        }

        return runHeadless(app,
                           parser.value(QStringLiteral("iface")),
                           parser.value(QStringLiteral("input-size")).toInt(),
                           parser.value(QStringLiteral("output-size")).toInt());
    }

    /* GUI mode */
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
