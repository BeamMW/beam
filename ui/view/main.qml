import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {
    id: main

    width: 1100
    height: 800
    color: "#032e48"

    property var contentItems : ["dashboard", "wallet", "notifications", "help", "settings"]
    property int selectedItem

    Rectangle {
        id: sidebar
        width: 70
        height: 0
        color: "#02253d"
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
            anchors.topMargin: 130

            Repeater{
                model: contentItems

                Item {
                    width: 70
                    height: 70

                    Image {
                        id: itemIcon
                        anchors.fill: parent
                        fillMode: Image.Stretch
                        source: "qrc:///assets/" + modelData + "-icon" + (selectedItem == index ? "-active" : "") + ".png"
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
            x: 0
            y: 0
            width: 50
            height: 50
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 50
            source: "qrc:///assets/logo.png"
        }
    }

    Loader {
        id: content
        anchors.topMargin: 54
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
