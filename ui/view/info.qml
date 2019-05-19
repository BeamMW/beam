import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {

    anchors.fill: parent
    color: Style.background_main

    Text {
        anchors.centerIn: parent
        font.pixelSize: 40
        color: Style.content_main
        //% "Info view"
        text: qsTrId("info-title")
    }
}
