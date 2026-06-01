#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    
    QApplication app(argc, argv);
    
    QApplication::setApplicationName("TiwutLauncher");
    QApplication::setOrganizationName("Tiwut");

    MainWindow w;
    w.resize(1024, 768);
    w.show();

    return app.exec();
}
