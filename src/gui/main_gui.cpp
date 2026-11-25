#include "MainWindow.hpp"

#include <QApplication>
#include <QCommandLineParser>
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
        QTimer::singleShot(300, &app, [&window, path, &app] {
            window.grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
