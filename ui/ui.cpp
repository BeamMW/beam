#include <QGuiApplication>
#include <QQuickView>
#include <QtQml>


int main (int argc, char* argv[])
{

    QGuiApplication q_app (argc, argv);
    
    // Using QQuickView
    QQuickView view;
    view.setSource(QUrl::fromLocalFile("qml/hw.qml"));
    view.show();
    
    return q_app.exec ();
}
