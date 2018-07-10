#include <QApplication>
#include <QQmlApplicationEngine>
#include <QtQml>
#include "qmltranslator.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Создаём объект для работы с переводами ...
    QmlTranslator qmlTranslator;

    QQmlApplicationEngine engine;
    // и регистрируем его в качестве контекста в Qml слое
    engine.rootContext()->setContextProperty("qmlTranslator", &qmlTranslator);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    return app.exec();
}
