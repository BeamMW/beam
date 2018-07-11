import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
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

            SecondaryPanel {
                width: (parent.width - parent.spacing)*346/864
                height: parent.height
                title: "Received"
                amountColor: Style.bright_sky_blue
                value: walletViewModel.received
            }
            
            visible: false
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
