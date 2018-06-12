import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Dialogs 1.1

Rectangle {
    id: page
    width: 320; height: 480
    color: "lightgray"

    Text {
        id: helloText
        objectName: "helloText"
        text: "Hello Beam Wallet!"
        y: 30
        anchors.horizontalCenter: page.horizontalCenter
        font.pointSize: 24; font.bold: true
    }

    MessageDialog {
            id: msg
            title: "Title"
            text: "Button pressed"
            onAccepted: visible = false
        }

    Button {
            text: "press me"
            onClicked: msg.visible = true
        }
}
