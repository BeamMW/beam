import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"

Rectangle {

    id: wallet_layout
    anchors.fill: parent
    color: Style.marine
    state: "wide"

    // Component.onCompleted:{
    //     console.log(FontLoader.Ready)
    // }

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
                width: (parent.width - 3*30)*500/1220
                height: parent.height
            }

            SecondaryPanel {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                title: "Received"
                amountColor: Style.bright_sky_blue
                value: walletViewModel.received
            }

            SecondaryPanel {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                title: "Sent"
                amountColor: Style.heliotrope
                value: walletViewModel.sent
            }

            SecondaryPanel {
                width: (parent.width - 3*30)*240/1220
                height: parent.height
                title: "Unconfirmed"
                amountColor: Style.white
                value: walletViewModel.unconfirmed
            }
        }

        Row {

            id: medium_panels

            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height

            spacing: 30

            AvailablePanel {
                width: (parent.width - parent.spacing)*518/864
                height: parent.height              
            }

            Item {
                id: panel_holder

                width: (parent.width - parent.spacing)*346/864
                height: parent.height
                state: "one"

                clip: true
                
                SecondaryPanel {
                    id: received_panel
                    title: "Received"
                    amountColor: Style.bright_sky_blue
                    value: walletViewModel.received
                    anchors.fill: parent
                    visible: true
                }

                SecondaryPanel {
                    id: sent_panel
                    title: "Sent"
                    amountColor: Style.heliotrope
                    value: walletViewModel.sent
                    anchors.fill: parent
                    visible: false
                }

                SecondaryPanel {
                    id: unconfirmed_panel
                    title: "Unconfirmed"
                    amountColor: Style.white
                    value: walletViewModel.unconfirmed
                    anchors.fill: parent
                    visible: false
                }

                Row {

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10

                    spacing: 10

                    Led {
                        id: led1

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel_holder.state = "one"
                        }
                    }
                    
                    Led {
                        id: led2

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel_holder.state = "two"
                        }
                    }

                    Led {
                        id: led3

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel_holder.state = "three"
                        }
                    }
                }

                states: [
                    State {
                        name: "one"
                        PropertyChanges {target: led1; turned_on: true}
                    },
                    State {
                        name: "two"
                        PropertyChanges {target: sent_panel; visible: true}
                        PropertyChanges {target: led2; turned_on: true}
                    },
                    State {
                        name: "three"
                        PropertyChanges {target: unconfirmed_panel; visible: true}
                        PropertyChanges {target: led3; turned_on: true}
                    }
                ]
            }

            
            visible: false
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
            width: (300-72)

            resizable: false
            movable: false
        }

        TableViewColumn {
            role: "recipient"
            title: "Recipient / Sender ID"
            width: (680-300)

            resizable: false
            movable: false
        }

        TableViewColumn {
            role: "comment"
            title: "Comment"
            width: (800-680)

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
            width: (1000-800)

            resizable: false
            movable: false

            delegate: Row {
                anchors.fill: parent
                spacing: 6

                property bool income: model["income"]

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
            role: "amount_usd"
            title: "Amount, USD"
            width: (1214-1000)

            resizable: false
            movable: false
        }

        TableViewColumn {
            role: "status"
            title: "Status"
            width: (34+62)

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

        model: ListModel {
            ListElement {
                income: true
                date: "12 June 2018  |  3:46 PM"
                recipient: "1Cs4wu6pu5qCZ35bSLNVzGyEx5N6uzbg9t"
                comment: "Thanks for your work!"
                amount: "0.63736"
                amount_usd: "726.4 USD"
                status: "received"
            }

            ListElement {
                income: false
                date: "10 June 2018  |  7:02 AM"
                recipient: "magic_stardust16"
                amount: "1.300"
                amount_usd: "10 726.4 USD"
                status: "sent"
            }
        }

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
        }
    }
    
    states: [
        State {
            name: "wide"
        },

        State {
            when: wallet_layout.width < (1440-70-2*30)
            name: "medium"
            PropertyChanges {target: wide_panels; visible: false}
            PropertyChanges {target: medium_panels; visible: true}
        },

        State {
            name: "small"
        }
    ]
}
