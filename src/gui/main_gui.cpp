#include "MainWindow.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    sap::gui::MainWindow window;
    window.show();
    return app.exec();
}
