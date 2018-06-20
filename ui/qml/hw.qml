import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {
    id: main

    width: 1100
    height: 800
    color: "#032e48"

    Rectangle {
        id: sidebar
        width: 70
        height: 0
        color: "#02253d"
        border.width: 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
    }

    Item {
        id: content
        anchors.topMargin: 30
        anchors.bottomMargin: 0
        anchors.rightMargin: 30
        anchors.leftMargin: 100
        anchors.fill: parent

        TableView {
            id: tx_table
            x: 30
            y: 438
            selectionMode: 1
            frameVisible: false
            anchors.rightMargin: 0
            anchors.leftMargin: 0
            anchors.topMargin: 400
            anchors.fill: parent
        }

        Text {
            id: tx_label
            x: 60
            y: 400
            color: "#ffffff"
            text: qsTr("Transactions")
            font.pixelSize: 18
            anchors.leftMargin: 30
            anchors.topMargin: 360
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
            anchors.topMargin: 120
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
                text: qsTr("0.628")
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
            anchors.topMargin: 120

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
                text: qsTr("0.221746")
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
            anchors.topMargin: 24
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
                horizontalAlignment: Text.AlignHCenter
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
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
            anchors.topMargin: 24
            anchors.right: parent.right
            anchors.rightMargin: 200

            Text {
                id: receive_btn_label
                color: "#b1ffff"
                text: qsTr("RECEIVE")
                styleColor: "#000000"
                style: Text.Normal
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 14
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
            anchors.topMargin: 80
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
            anchors.topMargin: 80
            anchors.top: parent.top
            anchors.left: parent.left
        }

        Text {
            id: page_label
            x: 30
            y: 54
            color: "#f7fff7"
            text: qsTr("Wallet")
            font.bold: false
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.top: parent.top
            anchors.topMargin: 24
            font.pixelSize: 28
        }








    }


}
