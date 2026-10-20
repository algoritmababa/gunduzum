#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TurbulenceLab");
    QApplication::setApplicationName("TurbulenceAnalysis");

    MainWindow w;
    w.show();
    return app.exec();
}
