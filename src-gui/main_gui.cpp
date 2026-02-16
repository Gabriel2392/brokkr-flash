#include <QApplication>
#include "brokkr_wrapper.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    BrokkrWrapper window;
    window.show();
    return app.exec();
}