import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"

Item {
    id: root
    anchors.fill: parent

    state: "wallet"

    SFText {
        font.pixelSize: 36
        color: Style.white
        text: "Wallet"
    }

    Rectangle {
        id: user_led
        y: 55

        width: 10
        height: 10

        radius: 5

        color: Style.bright_teal
    }

    DropShadow {
        anchors.fill: user_led
        radius: 5
        samples: 9
        color: Style.bright_teal
        source: user_led
    }

    SFText {
        x: 20
        y: 53

        font.pixelSize: 12
        color: Style.bluey_grey
        text: "1Cs4wu6pu5qCZ35bSLNVzGyEx5N6uzbg9t"
    }

    /////////////////////////////////////////////////////////////
    /// Send layout /////////////////////////////////////////////
    /////////////////////////////////////////////////////////////

    Rectangle {
        id: send_layout
        anchors.fill: parent
        anchors.topMargin: 97
        anchors.bottomMargin: 30

        radius: 10
        color: Style.dark_slate_blue

        Item {
            anchors.fill: parent
            anchors.topMargin: 30
            anchors.bottomMargin: 30
            anchors.leftMargin: 30
            anchors.rightMargin: 30

            clip: true

            Item {
                anchors.fill: parent
                anchors.rightMargin: parent.width/2
                anchors.bottomMargin: 60
                
                clip: true

                SFText {
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: Style.white
                    text: "Send BEAM"
                }

                Item {
                    anchors.fill: parent
                    anchors.rightMargin: parent.width/2

                    clip: true

                    SFText {

                        y: 41

                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: Style.white
                        text: "Recipient address"
                    }

                    SFTextInput {
                        id: receiver_addr

                        y: 115-30
                        width: 300

                        font.pixelSize: 12

                        color: Style.white

                        text: walletViewModel.receiverAddr

                    }

                    Binding {
                        target: walletViewModel
                        property: "receiverAddr"
                        value: receiver_addr.text
                    }

                    Rectangle {
                        y: 109
                        width: 300
                        height: 1

                        color: "#33566b"
                    }

                    AvailablePanel {
                        y: 140
                        width: parent.width
                        height: 206
                        value: walletViewModel.available*1 - (walletViewModel.sendAmount*1 + walletViewModel.sendAmountMils/1000000)
                    }                    
                }

                Item {
                    anchors.fill: parent
                    anchors.leftMargin: parent.width/2

                    clip: true


                    SFText {

                        y: 41

                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: Style.white
                        text: "Address book"
                    }

                    ListView {
                        id:addrBook

                        anchors.fill: parent
                        anchors.topMargin: 60
                        anchors.rightMargin: 30

                        model: walletViewModel.addrBook

                        delegate: Item {

                            height: 60
                            width: parent.width

                            SFText { 
                                text: modelData 
                                color: Style.white

                                anchors.verticalCenter: parent.verticalCenter
                                x: 10
                            }

                            MouseArea {
                                anchors.fill: parent

                                cursorShape: Qt.PointingHandCursor
                                onClicked: addrBook.currentIndex = index
                            }
                        }

                        highlight: Rectangle { color: "#33566b"; radius: 10 }
                    }

					Binding {
                        target: walletViewModel
                        property: "selectedAddr"
                        value: addrBook.currentIndex
                    }
                }
            }

            Item {
                anchors.fill: parent
                anchors.leftMargin: parent.width/2
                anchors.bottomMargin: 60

                SFText {
                    y: 41

                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: Style.white
                    text: "Transaction amount"
                }

                SFTextInput {
                    id: amount_input
                    y: 93-30
                    width: 300

                    font.pixelSize: 48
                    color: Style.heliotrope

                    text: walletViewModel.sendAmount

                    validator: IntValidator{bottom: 0; top: 210000000;}
                    selectByMouse: true
                }

                Binding {
                    target: walletViewModel
                    property: "sendAmount"
                    value: amount_input.text
                }

                Rectangle {
                    y: 153-30
                    width: 337
                    height: 1

                    color: "#33566b"
                }

                SFText {
                    x: 204+157
                    y: 117-30

                    font.pixelSize: 24
                    color: Style.white
                    text: "BEAM"
                }

                SFTextInput {
                    id: mils_amount_input
                    y: 93+30
                    width: 300

                    font.pixelSize: 48

                    color: Style.heliotrope

                    text: walletViewModel.sendAmountMils

                    validator: IntValidator{bottom: 0; top: 999999;}
                }

                Binding {
                    target: walletViewModel
                    property: "sendAmountMils"
                    value: mils_amount_input.text
                }

                Rectangle {
                    y: 153+30
                    width: 337
                    height: 1

                    color: "#33566b"
                }

                SFText {
                    x: 204+157
                    y: 117+30

                    font.pixelSize: 24
                    color: Style.white
                    text: "MIL"
                }

                SFText {
                    y: 164+30
                    opacity: 0.5
                    font.pixelSize: 24
                    font.weight: Font.ExtraLight
                    color: Style.white
                    text: (walletViewModel.sendAmount*1 + walletViewModel.sendAmountMils/1000000) + " USD"
                }

                /////////////////////////////////////////////////////////////
                /// Transaction fee /////////////////////////////////////////
                /////////////////////////////////////////////////////////////

                SFText {
                    y: 243   

                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: Style.white
                    text: "Transaction fee"
                }

                Rectangle {
                    id: fee_line
                    y: 303
                    width: 360
                    height: 4

                    opacity: 0.1
                    radius: 2

                    color: Style.white
                }

                Rectangle {
                    id: led

                    x: 140
                    y: 303-8

                    width: 20
                    height: 20

                    radius: 10

                    color: Style.bright_teal

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                DropShadow {
                    anchors.fill: led
                    radius: 5
                    samples: 9
                    color: Style.bright_teal
                    source: led
                }

                SFText {
                    y: 277

                    font.pixelSize: 12
                    color: Style.bluey_grey
                    text: "40h"
                }

                SFText {
                    y: 277
                    anchors.right: fee_line.right

                    font.pixelSize: 12
                    color: Style.bluey_grey
                    text: "20m"
                }

                SFText {
                    y: 319

                    font.pixelSize: 12
                    color: Style.bluey_grey
                    text: "0.0002"
                }

                SFText {
                    y: 319
                    anchors.right: fee_line.right

                    font.pixelSize: 12
                    color: Style.bluey_grey
                    text: "0.01"
                }

                /////////////////////////////////////////////////////////////
                /////////////////////////////////////////////////////////////
                /////////////////////////////////////////////////////////////

                SFText {
                    y: 383-30   

                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: Style.white
                    text: "Comment"
                }

                SFTextInput {
                    y: 427-30
                    width: 300

                    font.pixelSize: 12

                    color: Style.white

                    text: "Thank you for your work!"
                }

                Rectangle {
                    y: 451-30
                    width: 339
                    height: 1

                    color: "#33566b"
                }
            }

            Row {

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom

                spacing: 30

                Rectangle {
                    width: 130
                    height: 40

                    radius: 20

                    color: "#33566b"

                    SFText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: "CANCEL"

                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.state = "wallet"
                    }
                }

                Rectangle {
                    width: 108
                    height: 40

                    radius: 20

                    color: Style.heliotrope

                    SFText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: "SEND"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            walletViewModel.sendMoney()
                            root.state = "wallet"
                        }
                    }
                }                
            }

        }

        visible: false
    }

    Item {

        id: wallet_layout
        anchors.fill: parent
        state: "wide"

        CuteButton {
            anchors.top: parent.top
            anchors.right: parent.right

            label: "SEND"

            onClicked: root.state = "send"
        }

        Item {
            y: 97
            height: 206

            anchors.left: parent.left
            anchors.right: parent.right

            Row {

                id: wide_panels

                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.height

                spacing: 30

                AvailablePanel {
                    width: (parent.width - 30)*500/1220
                    height: parent.height
                    value: walletViewModel.available
                }

                // SecondaryPanel {
                //     width: (parent.width - 3*30)*240/1220
                //     height: parent.height
                //     title: "Received"
                //     amountColor: Style.bright_sky_blue
                //     value: walletViewModel.received
                // }

                // SecondaryPanel {
                //     width: (parent.width - 3*30)*240/1220
                //     height: parent.height
                //     title: "Sent"
                //     amountColor: Style.heliotrope
                //     value: walletViewModel.sent
                // }

                SecondaryPanel {
                    width: (parent.width - 30)*240*3/1220
                    height: parent.height
                    title: "Unconfirmed"
                    amountColor: Style.white
                    value: walletViewModel.unconfirmed
                }
            }
        }

        Item
        {
            y: 353

            anchors.left: parent.left
            anchors.right: parent.right

            SFText {
                x: 30

                font {
                    pixelSize: 18
                    weight: Font.Bold
                }

                color: Style.white

                text: "Transactions"
            }

            Row {

                anchors.right: parent.right
                spacing: 20
                state: "all"

                TxFilter{
                    id: all
                    label: "ALL"
                    onClicked: parent.state = "all"
                }

                TxFilter{
                    id: sent
                    label: "SENT"
                    onClicked: parent.state = "sent"
                }

                TxFilter{
                    id: received
                    label: "RECEIVED"
                    onClicked: parent.state = "received"
                }

                TxFilter{
                    id: in_progress
                    label: "IN PROGRESS"
                    onClicked: parent.state = "in_progress"
                }

                states: [
                    State {
                        name: "all"
                        PropertyChanges {target: all; state: "active"}
                    },
                    State {
                        name: "sent"
                        PropertyChanges {target: sent; state: "active"}
                    },
                    State {
                        name: "received"
                        PropertyChanges {target: received; state: "active"}
                    },
                    State {
                        name: "in_progress"
                        PropertyChanges {target: in_progress; state: "active"}
                    }
                ]
            }        
        }

        Rectangle {
            anchors.fill: parent;
            anchors.topMargin: 394

            radius: 10

            color: Style.dark_slate_blue
        }

        Rectangle {
            anchors.fill: parent;
            anchors.topMargin: 394+46

            color: "#0a344d"
        }

        TableView {

            id: tx_view

            anchors.fill: parent;
            anchors.topMargin: 394

            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false

            TableViewColumn {
                role: "income"
                width: 72

                resizable: false
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    SvgImage {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: styleData.value ? "qrc:///assets/icon-received.svg" : "qrc:///assets/icon-sent.svg"
                    }
                }
            }

            TableViewColumn {
                role: "date"
                title: "Date | time"
                width: 200

                resizable: false
                movable: false
            }

            TableViewColumn {
                role: "user"
                title: "Recipient / Sender ID"
                width: 200

                resizable: false
                movable: false
            }

            TableViewColumn {
                role: "comment"
                title: "Comment"
                width: 120

                resizable: false
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    SvgImage {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 20
                        source: "qrc:///assets/icon-comment.svg"
                        visible: styleData.value != null
                    }
                }
            }

            TableViewColumn {
                role: "amount"
                title: "Amount, BEAM"
                width: 200

                resizable: false
                movable: false

                delegate: Row {
                    anchors.fill: parent
                    spacing: 6

                    property bool income: tx_view.model[styleData.row].income

                    SFText {
                        font.pixelSize: 24

                        color: parent.income ? Style.bright_sky_blue : Style.heliotrope

                        anchors.verticalCenter: parent.verticalCenter
                        text: (parent.income ? "+ " : "- ") + styleData.value
                    }

                    SFText {
                        font.pixelSize: 12

                        color: parent.income ? Style.bright_sky_blue : Style.heliotrope

                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 4
                        text: "BEAM"
                    }
                }
            }

            TableViewColumn {
                role: "amountUsd"
                title: "Amount, USD"
                width: 200

                resizable: false
                movable: false
            }

            TableViewColumn {
                role: "change"
                title: "Change, BEAM"
                width: 200

                resizable: false
                movable: false
            }

            TableViewColumn {
                role: "status"
                title: "Status"
                width: 96

                resizable: false
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    SFText {
                        font.pixelSize: 12

                        color: {
                            if(styleData.value == "sent")
                                Style.heliotrope
                            else if(styleData.value == "received")
                                Style.bright_sky_blue
                            else Style.white
                        }

                        anchors.verticalCenter: parent.verticalCenter
                        text: styleData.value
                    }
                }
            }

            model: walletViewModel.tx

            headerDelegate: Item {
                height: 46

                SFText {
                    anchors.verticalCenter: parent.verticalCenter

                    font.pixelSize: 12
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            rowDelegate: Item {
                height: 69

                anchors.left: parent.left
                anchors.right: parent.right

                Rectangle {
                    anchors.fill: parent

                    color: Style.light_navy
                    visible: styleData.alternate
                }
            }

            itemDelegate: Item {

                anchors.fill: parent

                SFText {
                    anchors.verticalCenter: parent.verticalCenter

                    font.pixelSize: 12
                    color: Style.white

                    font.weight: Font.Normal

                    text: styleData.value
                }

                clip:true
            }
        }
        
        states: [
            State {
                name: "wide"
            },

            State {
                when: wallet_layout.visible && wallet_layout.width < (1440-70-2*30)
                name: "medium"
                // PropertyChanges {target: wide_panels; visible: false}
                // PropertyChanges {target: medium_panels; visible: true}
            },

            State {
                when: wallet_layout.visible && wallet_layout.width < (1440-70-2*30)
                name: "small"
                // PropertyChanges {target: wide_panels; visible: false}
                // PropertyChanges {target: medium_panels; visible: true}
            }
        ]
    }    

    states: [
        State {
            name: "wallet"
        },

        State {
            name: "send"
            PropertyChanges {target: wallet_layout; visible: false}
            PropertyChanges {target: send_layout; visible: true}
        }
    ]
}
