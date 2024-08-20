#include "server_manager.hpp"
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 2)
    {
        qWarning(" ERROR !!!!The Information required to connect to the Database should be entered as command line arguments : DB string URI -- > argv[1] ");

        return 1;
    }

    const QString &URI_string = QString::fromStdString(argv[1]);

    server_manager Main_window(URI_string);

    return app.exec();
}
