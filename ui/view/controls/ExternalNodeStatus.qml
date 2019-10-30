import QtQuick 2.11
import QtQuick.Controls 2.4
import QtGraphicalEffects 1.0
import Beam.Wallet 1.0
import "."

Item {

    id: rootItem

    property string status

    property int indicator_radius: 5
    property Item indicator: online_indicator

    Item {
        id: online_indicator
        anchors.top: parent.top
        anchors.left: parent.left
        width: childrenRect.width

        property color color: Style.content_main

        Rectangle {
            id: online_rect
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.topMargin: 2

            width: rootItem.indicator_radius * 2
            height: rootItem.indicator_radius * 2
            radius: rootItem.indicator_radius
            color: parent.color
            border.width: 1
        }

        DropShadow {
            anchors.fill: online_rect
            radius: 5
            samples: 9
            source: online_rect
            color: parent.color
        }
    }

    states: [
        State {
            name: "uninitialized"
            when: (rootItem.status === "uninitialized")
            PropertyChanges { target: online_rect; border.color: Style.content_main }
            PropertyChanges { target: online_indicator; color: "transparent" }
            PropertyChanges { target: online_indicator; opacity: 0.3 }
        },
        State {
            name: "disconnected"
            when: (rootItem.status === "disconnected")
            PropertyChanges { target: online_rect; border.color: Style.content_main }
            PropertyChanges { target: online_indicator; color: Style.content_main }
            PropertyChanges { target: online_indicator; opacity: 0.3 }
        },
        State {
            name: "connected"
            when: (rootItem.status === "connected")
            PropertyChanges { target: online_rect; border.color: Style.active }
            PropertyChanges { target: online_indicator; color: Style.active }
            PropertyChanges { target: online_indicator; opacity: 1 }
        },
        State {
            name: "error"
            when: (rootItem.status === "error")
            PropertyChanges { target: online_rect; border.color: Style.accent_fail }
            PropertyChanges { target: online_indicator; color: Style.accent_fail }
            PropertyChanges { target: online_indicator; opacity: 1 }
        }
    ]
}
