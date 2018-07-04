import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Item {
    width: 1024
    height: 768

    Rectangle {
        color: "#053d5a"
        radius: 10
        anchors.topMargin: 96
        anchors.bottomMargin: 240
        anchors.fill: parent

        Item {
            anchors.rightMargin: 30
            anchors.leftMargin: 30
            anchors.bottomMargin: 30
            anchors.topMargin: 30
            anchors.fill: parent

            Rectangle {
                id: send_btn
                width: 100
                height: 40

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

                radius: 20
                border.width: 0
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 0
                anchors.horizontalCenterOffset: 65
                anchors.horizontalCenter: parent.horizontalCenter

                Text {
                    color: "#ffffff"
                    text: qsTr("SEND")
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    anchors.fill: parent
                    font.pixelSize: 18
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        walletViewModel.sendMoney()
						walletLayout.state = ""
                    }
                }
            }

            Rectangle {
                id: cancel_btn
                x: 2
                y: -7
                width: 100
                height: 40
                color: "#023451"
                radius: 20
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 0
                anchors.bottom: parent.bottom
                anchors.horizontalCenterOffset: -65
                border.width: 0
                Text {
                    color: "#ffffff"
                    text: qsTr("CANCEL")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 18
                    anchors.fill: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        walletLayout.state = ""
                    }
                }
            }

            Text {
                color: "#bce7f6"
                text: qsTr("Send BEAM")
                font.bold: true
                anchors.left: parent.left
                anchors.leftMargin: 0
                anchors.top: parent.top
                anchors.topMargin: 0
                font.pixelSize: 16
            }

            Item {
                width: parent.width*0.5-15
                anchors.left: parent.left
                anchors.leftMargin: 0
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 70
                anchors.top: parent.top
                anchors.topMargin: 30

                Text {
                    color: "#bce7f6"
                    text: qsTr("Recipient ID")
                    font.pixelSize: 12
                }

                TextInput {
                    id: receiverAddr
                    height: 20
                    color: "#bce7f6"
                    text: walletViewModel.receiverAddr
                    anchors.right: parent.right
                    anchors.rightMargin: 0
                    horizontalAlignment: Text.AlignLeft
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 60
                    font.pixelSize: 12
                }

                Binding {
                    target: walletViewModel
                    property: "receiverAddr"
                    value: receiverAddr.text
                }

                Rectangle {
                    height: 1
                    color: "#17506d"
                    anchors.right: parent.right
                    anchors.rightMargin: 0
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 90
                }

                Rectangle {
                    x: 30
                    y: 105
                    width: 12
                    height: 12
                    color: "#00f9c9"
                    radius: 6
                    anchors.leftMargin: 0
                    anchors.topMargin: 120
                    border.width: 0
                    anchors.top: parent.top
                    anchors.left: parent.left
                }

                Text {
                    x: 50
                    y: 104
                    color: "#527b96"
                    text: qsTr("magic_stardust16")
                    font.pixelSize: 12
                    anchors.leftMargin: 20
                    anchors.topMargin: 120
                    anchors.top: parent.top
                    anchors.left: parent.left
                }
            }

            Item {
                x: -5
                y: -7
                width: parent.width*0.5-15
                anchors.right: parent.right
                anchors.rightMargin: 0
                anchors.topMargin: 30
                anchors.bottomMargin: 70
                anchors.bottom: parent.bottom
                anchors.top: parent.top

                Text {
                    color: "#ffffff"
                    text: qsTr("Transaction amount")
                    font.pixelSize: 12
                }

                TextInput {
                    id: amountInput
                    validator: IntValidator{bottom: 1; top: 10000;}
                    focus: true
                    height: 40
                    color: "#c678e9"
                    text: walletViewModel.sendAmount
                    cursorVisible: false
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 40
                    font.pixelSize: 40
                }

                Binding {
                    target: walletViewModel
                    property: "sendAmount"
                    value: amountInput.text
                }

                Rectangle {
                    x: -9
                    y: -8
                    height: 1
                    color: "#17506d"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 90
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.rightMargin: 0
                }

                Text {
                    color: "#72a4c1"
                    text: qsTr("1339.2 USD")
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 120
                    font.pixelSize: 20
                }

                Text {
                    color: "#ffffff"
                    text: qsTr("Transaction fee")
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 180
                    font.pixelSize: 12
                }

                Item {
                    anchors.right: parent.right
                    anchors.rightMargin: 0
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 0
                    anchors.left: parent.left
                    anchors.leftMargin: 0
                    anchors.top: parent.top
                    anchors.topMargin: 220

                    Text {
                        color: "#ffffff"
                        text: qsTr("12 hours / 0.000834 (2%)")
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                        font.pixelSize: 12
                    }

                    Text {
                        color: "#184e6e"
                        text: qsTr("48h")
                        font.bold: true
                        anchors.left: parent.left
                        anchors.leftMargin: 0
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        font.pixelSize: 12
                    }

                    Text {
                        x: 0
                        y: 0
                        color: "#184e6e"
                        text: qsTr("0.0002")
                        anchors.top: parent.top
                        anchors.topMargin: 40
                        anchors.left: parent.left
                        anchors.leftMargin: 0
                        font.bold: true
                        font.pixelSize: 12
                    }

                    Text {
                        x: -2
                        color: "#184e6e"
                        text: qsTr("20m")
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 0
                        font.bold: true
                        font.pixelSize: 12
                    }

                    Text {
                        x: -2
                        y: -2
                        color: "#184e6e"
                        text: qsTr("0.01")
                        anchors.right: parent.right
                        anchors.rightMargin: 0
                        anchors.top: parent.top
                        anchors.topMargin: 40
                        font.bold: true
                        font.pixelSize: 12
                    }

                    Rectangle {
                        width: 200
                        height: 4
                        color: "#184e6e"
                        border.width: 0
                        anchors.top: parent.top
                        anchors.topMargin: 30
                        anchors.right: parent.right
                        anchors.rightMargin: 0
                        anchors.left: parent.left
                        anchors.leftMargin: 0
                    }

                    Rectangle {
                        width: 20
                        height: 20
                        color: "#09f4d2"
                        radius: 10
                        anchors.top: parent.top
                        anchors.topMargin: 22
                        anchors.horizontalCenter: parent.horizontalCenter
                        border.width: 0
                    }
                }
            }
        }
    }

    Item {
        width: 1000
        height: 210
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.left: parent.left

        Rectangle {
            id: available
            x: 30
            y: 145
            width: parent.width*0.5-15
            color: "#1d425d"
            radius: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            clip: true
            border.width: 0
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.top: parent.top
            anchors.topMargin: 0

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
            id: unconfirmed
            x: 512
            y: 145
            width: parent.width*0.5-15
            color: "#1d425d"
            radius: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            clip: true
            anchors.right: parent.right
            anchors.rightMargin: 0
            border.width: 0
            anchors.topMargin: 0
            anchors.top: parent.top

            Text {
                id: unconfirmed_label
                color: "#ffffff"
                text: qsTr("Unconfirmed")
                font.pixelSize: 18
                anchors.leftMargin: 30
                anchors.topMargin: 30
                font.bold: true
                anchors.top: parent.top
                anchors.left: parent.left
            }

            Text {
                id: unconfirmed_amount
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
                id: unconfirmed_usd
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

    }
}
