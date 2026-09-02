#include "controller.hpp"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2) return 2;
    Controller controller(QString::fromLocal8Bit(argv[1]));
    QObject::connect(&controller,&Controller::statusReceived,&app,[](const QJsonObject&) { std::cout<<"ready"<<std::endl; });
    controller.start();
    return app.exec();
}
