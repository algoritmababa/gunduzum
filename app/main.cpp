#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TurbulenceLab");
    QApplication::setApplicationName("TurbulenceMeasurement");

    MainWindow w;
    w.show();
    return app.exec();
}
