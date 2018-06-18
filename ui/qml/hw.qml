import QtQuick 2.2
import QtQuick.Controls 1.0

Rectangle {
    width: 360
    height: 360

    Column {
        id: column
        spacing: 0
        anchors.fill: parent

        Text {
            id: text1
            text: model.label
            horizontalAlignment: Text.AlignLeft
            font.pixelSize: 12
        }

        Button {
            id: button
            text: qsTr("Button")
            onClicked: model.sayHello("Beam")
        }
    }
}
