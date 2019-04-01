import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Item {
    id: root

    width: 6
    height: 6

    property bool turned_on: false
    signal clicked()

    Rectangle {
        anchors.fill: parent
        radius: 3

        color: Style.content_secondary
    }

    Rectangle {
        id: led_light
        anchors.fill: parent
        radius: 3

        color: Style.active
        visible: root.turned_on
    }

    DropShadow {
        anchors.fill: led_light
        radius: 5
        samples: 9
        color: Style.active
        source: led_light
        visible: root.turned_on
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
