import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {
    id: rectangle

    width: 800
    height: 600

    Text {
        id: text2
        text: qsTr("Transactions")
        font.bold: true
        anchors.top: parent.top
        anchors.topMargin: 50
        anchors.left: parent.left
        anchors.leftMargin: 0
        font.pixelSize: 16
    }

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

    TableView {
        id: tableView
        height: 202
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 75

        TableViewColumn {
            id: dateColumn
            title: "Date | time"
            role: "date"
            movable: false
        }

        TableViewColumn {
            id: userColumn
            title: "User ID"
            role: "user"
            movable: false
        }

        TableViewColumn {
            id: commentColumn
            title: "Comment"
            role: "comment"
            movable: false
        }

        TableViewColumn {
            id: amountColumn
            title: "Amount, BEAM"
            role: "amount"
            movable: false
        }

        TableViewColumn {
            id: amountUsdColumn
            title: "Amount, USD"
            role: "amountUsd"
            movable: false
        }

        TableViewColumn {
            id: amountstatusColumn
            title: "Status"
            role: "status"
            movable: false
        }

        style: TableViewStyle {
            headerDelegate: Rectangle {
                height: 40
                color: "lightsteelblue"
                Text {
                    id: textItem
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: styleData.textAlignment
                    anchors.leftMargin: 12
                    text: styleData.value
                    elide: Text.ElideRight
                    color: textColor
                    renderType: Text.NativeRendering
                }
            }
        }

        model: listModel
    }

    ListView {
        id: listView
        width: 415
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 283
        delegate: Item {
            height: 20
            Row {
                Text {
                    text: user
                    anchors.verticalCenter: parent.verticalCenter
                }
                spacing: 10
            }
        }
        model: listModel
    }

    Rectangle {
        id: rectangle1
        color: "#ca4b4b"
        radius: 20
        anchors.rightMargin: 71
        anchors.bottomMargin: 55
        anchors.leftMargin: 421
        anchors.topMargin: 314
        anchors.fill: parent
    }
}
