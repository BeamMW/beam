import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"

Rectangle {
    id: main

    width: 1440
    height: 800
    color: Style.marine


    property var contentItems : ["dashboard", "wallet", "notification", "info", "settings"]
    property int selectedItem

    Rectangle {
        id: sidebar
        width: 70
        height: 0
        color: Style.navy
        border.width: 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top


        Column {
            width: 0
            height: 0
            anchors.right: parent.right
            anchors.rightMargin: 0
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.top: parent.top
            anchors.topMargin: 125

            Repeater{
                model: contentItems

                Item {
                    width: parent.width
                    height: parent.width

                    SvgImage {
                        id: itemIcon
                        x: 21
                        y: 16
                        width: 28
                        height: 28
                        source: "qrc:///assets/icon-" + modelData + (selectedItem == index ? "-active" : "") + ".svg"
                    }

                    Item {
                        Rectangle {
                            id: indicator
                            y: 6
                            width: 4
                            height: 48
                            color: Style.bright_teal
                        }

                        DropShadow {
                            anchors.fill: indicator
                            radius: 5
                            samples: 9
                            color: Style.bright_teal
                            source: indicator
                        }                        

    					visible: selectedItem == index
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: updateItem(index)
                    }
                }
            }
        }

        Image {
            id: image
            x: 20
            y: 50
            width: 30
            height: 24
            source: Style.logo
        }

    }

    Loader {
        id: content
        anchors.topMargin: 50
        anchors.bottomMargin: 0
        anchors.rightMargin: 30
        anchors.leftMargin: 100
        anchors.fill: parent
    }

    function updateItem(index)
    {
        selectedItem = index
        content.source = "qrc:///" + contentItems[index] + ".qml"
        mainViewModel.update(index)
    }

    Component.onCompleted:{
        updateItem(1) // load wallet view by default
    }
}
