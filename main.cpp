#include "server_manager.hpp"
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    server_manager Main_window(&app);

    return app.exec();
}