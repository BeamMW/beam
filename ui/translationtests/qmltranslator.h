#ifndef QMLTRANSLATOR_H
#define QMLTRANSLATOR_H

#include <QObject>
#include <QTranslator>

class QmlTranslator : public QObject
{
    Q_OBJECT

public:
    explicit QmlTranslator(QObject *parent = 0);

signals:
    // Сигнал об изменении текущего языка для изменения перевода интерфейса
    void languageChanged();

public:
    // Метод установки перевода, который будет доступен в QML
    Q_INVOKABLE void setTranslation(QString translation);

private:
    QTranslator m_translator;
};

#endif // QMLTRANSLATOR_H
