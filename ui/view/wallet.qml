import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {
    id: main

    width: 1100
    height: 800
    color: "#032e48"

    Text {
        id: tx_label
        x: 60
        y: 400
        color: "#ffffff"
        text: qsTr("Transactions")
        font.pixelSize: 18
        anchors.leftMargin: 30
        anchors.topMargin: 336
        font.bold: true
        anchors.top: parent.top
        anchors.left: parent.left
    }

    Rectangle {
        id: sent
        x: 512
        y: 145
        width: parent.width*0.5-15
        height: 200
        color: "#1d425d"
        radius: 10
        clip: true
        anchors.right: parent.right
        anchors.rightMargin: 0
        border.width: 0
        anchors.topMargin: 96
        anchors.top: parent.top

        Text {
            id: sent_label
            color: "#ffffff"
            text: qsTr("Sent")
            font.pixelSize: 18
            anchors.leftMargin: 30
            anchors.topMargin: 30
            font.bold: true
            anchors.top: parent.top
            anchors.left: parent.left
        }

        Text {
            id: sent_amount
            x: -4
            y: -4
            color: "#9f89ce"
            text: qsTr("0.628 BEAM")
            verticalAlignment: Text.AlignVCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            font.family: "Arial"
            styleColor: "#000000"
            font.italic: false
            font.underline: false
            font.pixelSize: 48
            anchors.leftMargin: 30
            style: Text.Normal
            anchors.topMargin: 0
            font.weight: Font.Normal
            anchors.top: parent.top
            anchors.left: parent.left
        }

        Text {
            id: sent_usd
            x: 0
            y: 2
            color: "#86a8c1"
            text: qsTr("339.2 USD")
            styleColor: "#000000"
            font.family: "Arial"
            anchors.bottom: parent.bottom
            font.underline: false
            font.italic: false
            anchors.leftMargin: 30
            font.pixelSize: 18
            style: Text.Normal
            anchors.bottomMargin: 30
            font.weight: Font.Normal
            anchors.left: parent.left
        }
    }

    Rectangle {
        id: available
        x: 30
        y: 145
        width: parent.width*0.5-15
        height: 200
        color: "#1d425d"
        radius: 10
        clip: true
        border.width: 0
        anchors.left: parent.left
        anchors.leftMargin: 0
        anchors.top: parent.top
        anchors.topMargin: 96

        Text {
            id: availabl_label
            color: "#ffffff"
            text: qsTr("Available")
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.top: parent.top
            anchors.topMargin: 30
            font.bold: true
            font.pixelSize: 18
        }

        Text {
            id: available_amount
            color: "#39d4c6"
            text: walletViewModel.available
            smooth: true
            antialiasing: true
            verticalAlignment: Text.AlignVCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            font.weight: Font.Normal
            font.underline: false
            font.italic: false
            styleColor: "#000000"
            style: Text.Normal
            font.family: "Arial"
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.top: parent.top
            anchors.topMargin: 0
            font.pixelSize: 48
        }

        Text {
            id: available_usd
            x: 0
            y: 2
            color: "#86a8c1"
            text: qsTr("1.6528 USD")
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30
            font.family: "Arial"
            styleColor: "#000000"
            font.italic: false
            font.underline: false
            font.pixelSize: 18
            anchors.leftMargin: 30
            style: Text.Normal
            font.weight: Font.Normal
            anchors.left: parent.left
        }
    }

    Rectangle {
        id: send_btn
        x: 830
        y: 54
        width: 170
        height: 60
        radius: 30
        border.color: "#000000"
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#dd72f3"
            }

            GradientStop {
                position: 1
                color: "#be44ea"
            }
        }
        anchors.topMargin: 0
        anchors.right: parent.right
        anchors.rightMargin: 0
        border.width: 0
        anchors.top: parent.top

        Text {
            id: send_btn_label
            x: -2
            y: 7
            color: "#ffeaff"
            text: qsTr("SEND")
            styleColor: "#000000"
            font.pixelSize: 14
            style: Text.Normal
            font.bold: true
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
        }
    }

    Rectangle {
        id: receive_btn
        x: 630
        y: 54
        width: 170
        height: 60
        radius: 30
        gradient: Gradient {
            GradientStop {
                position: 0
                color: "#08d1fb"
            }

            GradientStop {
                position: 1
                color: "#1ba4e5"
            }
        }
        border.width: 0
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.right: parent.right
        anchors.rightMargin: 200

        Text {
            id: receive_btn_label
            color: "#b1ffff"
            text: qsTr("RECEIVE")
            styleColor: "#000000"
            style: Text.Normal
            font.bold: true
            anchors.centerIn: parent
            font.pixelSize: 14
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
        }
    }

    Rectangle {
        id: net_status_led
        x: 30
        y: 105
        width: 12
        height: 12
        color: "#00f9c9"
        radius: 6
        anchors.leftMargin: 0
        anchors.topMargin: 56
        border.width: 0
        anchors.top: parent.top
        anchors.left: parent.left
    }

    Text {
        id: user_id_label
        x: 50
        y: 104
        color: "#527b96"
        text: qsTr("!lhfkjhHKJLHjh6743khKwe53453")
        font.pixelSize: 12
        anchors.leftMargin: 20
        anchors.topMargin: 56
        anchors.top: parent.top
        anchors.left: parent.left
    }

    Text {
        id: page_label
        x: 30
        y: 54
        color: "#f7fff7"
        text: qsTr("Wallet")
        font.family: "Arial"
        font.bold: false
        anchors.left: parent.left
        anchors.leftMargin: 0
        anchors.top: parent.top
        anchors.topMargin: 0
        font.pixelSize: 28
    }

    TableView {
        id: tx_table
        x: 30
        y: 438
        selectionMode: 1
        frameVisible: false
        anchors.rightMargin: 0
        anchors.leftMargin: 0
        anchors.topMargin: 376
        anchors.fill: parent

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

        model: listModel
    }
}
