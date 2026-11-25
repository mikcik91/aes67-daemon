#include "MainWindow.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QProcessEnvironment>
#include <QTimer>

// Headless-friendly: `--screenshot out.png` renders the window offscreen,
// saves a PNG after 300 ms and exits (useful for CI / WSL without a display).
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("AES67 / Dante SAP discovery GUI");
    parser.addHelpOption();
    const QCommandLineOption shotOpt("screenshot",
        "Render the window offscreen, save PNG to <file> and exit.",
        "file");
    parser.addOption(shotOpt);
    parser.process(app);

    sap::gui::MainWindow window;
    window.show();

    if (parser.isSet(shotOpt)) {
        const QString path = parser.value(shotOpt);
        // Delay is configurable via AES67_SCREENSHOT_DELAY_MS (default 300) so a
        // live test can let SAP packets arrive before the grab.
        const auto env = QProcessEnvironment::systemEnvironment();
        const int delayMs = env.value(QStringLiteral("AES67_SCREENSHOT_DELAY_MS"),
                                      QStringLiteral("300")).toInt();
        QTimer::singleShot(delayMs, &app, [&window, path, &app] {
            window.grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
