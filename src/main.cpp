#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    // 适配高分屏
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);
    
    MainWindow w;
    w.show();
    
    return a.exec();
}