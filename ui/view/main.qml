import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {
    id: main

    width: 1100
    height: 800
    color: "#032e48"

    property var contentItems : ["dashboard", "wallet", "notifications", "help", "settings"]
    property int selectedItem : 1 // wallet item

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
                        anchors.fill: parent
                        fillMode: Image.Stretch
                        source: "qrc:///assets/" + modelData + "-icon.png"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {content.source = "qrc:///" + modelData + ".qml"}
                    }
                }
            }
        }
    }

    Loader {
        id: content
        anchors.topMargin: 54
        anchors.bottomMargin: 0
        anchors.rightMargin: 30
        anchors.leftMargin: 100
        anchors.fill: parent

        source: "qrc:///" + contentItems[selectedItem] + ".qml"
    }
}
