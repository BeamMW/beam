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
