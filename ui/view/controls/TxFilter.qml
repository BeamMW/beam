import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Item  {
    id: root
    
    property string label
    property var capitalization: Font.MixedCase

    width: text_label.width
    height: 20

    state: "normal"

    signal clicked()

    SFText {
        id: text_label

        anchors.horizontalCenter: parent.horizontalCenter

        font.pixelSize: 12
        font.styleName: "Bold"; font.weight: Font.Bold
        font.capitalization: capitalization
        color: Style.content_main
        opacity: 0.4
        text: label

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }
    }

    Rectangle {
        id: led

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom

        width: text_label.width + 16
        height: 2

        color: Style.active

        visible: false
    }

    DropShadow {
        anchors.fill: led
        radius: 5
        samples: 9
        color: Style.active
        source: led

        visible: led.visible
    }

    states: [
        State {
            name: "normal"
        },
        State {
            name: "active"

            PropertyChanges {target: led; visible: true}
            PropertyChanges {target: text_label; opacity: 1.0}
        }
    ]
}
