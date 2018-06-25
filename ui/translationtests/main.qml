import QtQuick 2.6
import QtQuick.Controls 1.5

ApplicationWindow {
    id: applicationWindow
    visible: true
    width: 640
    height: 480
	title: qsTr("Hello World trghg gh g")

    Label {
        id: helloLabel
        height: 50
        anchors {
            top: parent.top
            left: parent.left
            right: parent.horizontalCenter
            margins: 10
        }
    }

    ComboBox {
        id: comboBox
        anchors {
            top: parent.top
            left: parent.horizontalCenter
            right: parent.right
            margins: 10
        }

        model: ["ru_RU", "en_US"]

        // При изменении текста, инициализируем установку перевода через С++ слой
        onCurrentTextChanged: {
            qmlTranslator.setTranslation(comboBox.currentText)
        }
    }

    Label {
        id: labelText
        wrapMode: Text.Wrap
        anchors {
            top: helloLabel.bottom
            left: parent.left
            right: parent.right
            margins: 10
        }
    }

    // Подключаемся к объекту переводчика
    Connections {
        target: qmlTranslator   // был зарегистрирован в main.cpp
        onLanguageChanged: {    // при получении сигнала изменения языка
            retranslateUi()     // инициализируем перевод интерфейса
        }
    }

    // Функция перевода интерфейса
    function retranslateUi() {
        applicationWindow.title = qsTr("Hello World")
        helloLabel.text = qsTr("Hello World")
        labelText.text = qsTr("The QTranslator class provides internationalization" +
                              "support for text output.An object of this class contains " +
                              "a set of translations from a source language to a target language. " +
                              "QTranslator provides functions to look up translations in a translation file. " +
                              "Translation files are created using Qt Linguist.")
    }

    Component.onCompleted: {
        retranslateUi();
    }
}
