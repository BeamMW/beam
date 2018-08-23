import QtQuick 2.6
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Wallet 1.0
import "controls"

Item {
    id: root
    anchors.fill: parent

    property bool toSend: false

    state: "wallet"

    ConfirmationDialog {
        id: confirmationDialog
        okButtonText: "send"
        onAccepted: {
            walletViewModel.sendMoney()
            root.state = "wallet"
        }
    }

    SFText {
        font.pixelSize: 36
        color: Style.white
        text: "Wallet"
    }

    Rectangle {
        id: user_led
        y: 55
        x: 5
        width: 10
        height: 10

        radius: 5

        color: (walletViewModel.isSyncInProgress == false) ? Style.bright_teal : "red"
    }

    DropShadow {
        anchors.fill: user_led
        radius: 5
        samples: 9
        color: (walletViewModel.isSyncInProgress == false) ? Style.bright_teal : "red"
        source: user_led
    }

    SFText {
        id: linkBtn
        x: 20
        y: 53

        font.pixelSize: 12
        color: Style.bluey_grey
        linkColor: Style.white

        text: {
            if(walletViewModel.syncProgress < 0)
                "Last update time: " + walletViewModel.syncTime + " (<a href=\"update\">update</a>)"
            else
                "Updating, please wait... [" + walletViewModel.syncProgress + "%]"
        }

        onLinkActivated: {
            if(link == "update")
            {
                walletViewModel.syncWithNode()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
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

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 30
            anchors.bottomMargin: 30
            anchors.leftMargin: 30
            anchors.rightMargin: 30

            clip: true

            RowLayout {
                Layout.fillWidth: true
                //Layout.fillHeight: true
                height: 300

                spacing: 30

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                
                    clip: true

                    SFText {
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: qsTr("Send BEAM")
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        Layout.rightMargin: 30
                        clip: true                                              

                        SFText {
                            Layout.topMargin: 41
                            Layout.minimumHeight: 14
                            Layout.maximumHeight: 14                           

                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("My address")
                        }                    

                        AddressComboBox {
                            id: senderAddrCombo
                            Layout.fillWidth: true
                            Layout.minimumHeight: 18
                            Layout.maximumHeight: 18
                            editable: true
                            model: addressBookViewModel.ownAddresses
                            editText: walletViewModel.senderAddr
                            color: Style.white
                            font.pixelSize: 14
                            validator: RegExpValidator { regExp: /[0-9a-fA-F]{1,64}/ }
                            //focus: true
                            onEditTextChanged: {
                                var i = find(editText);
                                senderName.text = i >= 0 ? addressBookViewModel.ownAddresses[i].name : "";
                            }
                        } 
                        SFText {
                            Layout.topMargin: 10
                            id: senderName
                            Layout.fillWidth: true
                            Layout.minimumHeight: 18
                            color: Style.white
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }
                        
                        SFText {                            
                            Layout.minimumHeight: 14
                            Layout.maximumHeight: 14
                            Layout.topMargin: 30

                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Peer address")
                        }

                        AddressComboBox {
                            id: receiverAddrCombo
                            Layout.fillWidth: true
                            Layout.minimumHeight: 18
                            Layout.maximumHeight: 18
                            editable: true
                            editText: walletViewModel.receiverAddr
                            model: addressBookViewModel.peerAddresses
                            color: Style.white
                            font.pixelSize: 14
                            validator: RegExpValidator { regExp: /[0-9a-fA-F]{1,64}/ }
                            onEditTextChanged: {
                                var i = find(editText);
                                receiverName.text = i >= 0 ? addressBookViewModel.peerAddresses[i].name : "";
                            }
                        }

                        SFText {
                            Layout.topMargin: 10
                            id: receiverName
                            Layout.fillWidth: true
                            Layout.minimumHeight: 18
                            color: Style.white
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }

                        Binding {
                             target: walletViewModel
                             property: "senderAddr"
                             value: senderAddrCombo.editText
                        }

                        Binding {
                             target: walletViewModel
                             property: "receiverAddr"
                             value: receiverAddrCombo.editText
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                
                    clip: true


                    ColumnLayout {

                        anchors.fill: parent
                        clip: true  

                        SFText {
                            id: amount_text

                            Layout.topMargin: 41

                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Transaction amount")
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true

                                SFTextInput {
                                    id: amount_input
                                    Layout.fillWidth: true

                                    font.pixelSize: 48
                                    color: Style.heliotrope

                                    text: walletViewModel.sendAmount

                                    validator: IntValidator{bottom: 0; top: 210000000;}
                                    selectByMouse: true
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1

                                    color: Style.separator_color
                                }                                
                            }

                            SFText {
                                font.pixelSize: 24
                                color: Style.white
                                text: qsTr("BEAM")
                            }

                            Binding {
                                target: walletViewModel
                                property: "sendAmount"
                                value: amount_input.text
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true

                                SFTextInput {
                                    id: mils_amount_input
                                    Layout.fillWidth: true

                                    font.pixelSize: 48
                                    color: Style.heliotrope

                                    text: walletViewModel.sendAmountMils

                                    validator: IntValidator{bottom: 0; top: 999999;}
                                    selectByMouse: true
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1

                                    color: Style.separator_color
                                }                                
                            }

                            SFText {
                                font.pixelSize: 24
                                color: Style.white
                                text: qsTr("GROTH")
                            }

                            Binding {
                                target: walletViewModel
                                property: "sendAmountMils"
                                value: mils_amount_input.text
                            }
                        }

                        SFText {
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Transaction fee")
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true

                                SFTextInput {
                                    id: mils_fee_input
                                    Layout.fillWidth: true

                                    font.pixelSize: 48
                                    color: Style.heliotrope

                                    text: walletViewModel.feeMils

                                    validator: IntValidator{bottom: 0; top: 999999;}
                                    selectByMouse: true
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1

                                    color: Style.separator_color
                                }                                
                            }

                            SFText {
                                font.pixelSize: 24
                                color: Style.white
                                text: qsTr("GROTH")
                            }

                            Binding {
                                target: walletViewModel
                                property: "feeMils"
                                value: mils_fee_input.text
                            }
                        }
                        
                        SFText {
                            opacity: 0.5
                            font.pixelSize: 24
                            font.weight: Font.ExtraLight
                            color: Style.white
                            text: (walletViewModel.sendAmount*1 + (walletViewModel.sendAmountMils*1 + walletViewModel.feeMils*1)/1000000) + " " + qsTr("BEAM")
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignCenter 
                Layout.fillWidth: false
                Layout.fillHeight: true
                height: 40

                spacing: 30

                CustomButton {
                    width: 122
                    text: qsTr("cancel")
                    palette.buttonText: Style.white
                    palette.button: Style.white
                    textOpacity: 0.1
                    icon.source: "qrc:///assets/icon-cancel.svg"
                    onClicked: root.state = "wallet"
                }

                CustomButton {
                    width: 149
                    text: qsTr("send money")
                    palette.buttonText: Style.marine
                    palette.button: Style.heliotrope
                    icon.source: "qrc:///assets/icon-send.svg"
                    onClicked: {
                        var message = "You are about to send %1 to address %2";
                        var beams = (walletViewModel.sendAmount*1 + (walletViewModel.sendAmountMils*1 + walletViewModel.feeMils*1)/1000000) + " " + qsTr("BEAM");

                        confirmationDialog.text = message.arg(beams).arg(walletViewModel.receiverAddr);
                        confirmationDialog.open();                        
                    }
                }
            }
        }

        AvailablePanel {
            color: "transparent"
            anchors.fill: parent
            anchors.topMargin: 300
            // Layout.leftMargin:-27
            value: walletViewModel.actualAvailable
        }

        visible: false
    }

    Item {

        id: wallet_layout
        anchors.fill: parent
        state: "wide"

        CustomButton {
            anchors.top: parent.top
            anchors.right: parent.right
            palette.button: Style.heliotrope
            palette.buttonText: Style.marine
            text: qsTr("SEND")

            onClicked: root.state = "send"
        }

        Item {
            y: 97
            height: 206

            anchors.left: parent.left
            anchors.right: parent.right

            RowLayout {

                id: wide_panels

                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.height

                spacing: 30

                AvailablePanel {
                    Layout.maximumWidth: 700
                    Layout.minimumWidth: 350
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    
                    value: walletViewModel.available
                }

                SecondaryPanel {
                    Layout.minimumWidth: 350
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    title: qsTr("Unconfirmed")
                    amountColor: Style.white
                    value: walletViewModel.unconfirmed
                }
            }
        }

        Item
        {
            y: 320

            anchors.left: parent.left
            anchors.right: parent.right

            SFText {
                x: 30

                font {
                    pixelSize: 18
                    weight: Font.Bold
                }

                color: Style.white

                text: qsTr("Transactions")
            }

//            Row {
//
//                anchors.right: parent.right
//                spacing: 20
//                state: "all"
//
//                TxFilter{
//                    id: all
//                    label: "ALL"
//                    onClicked: parent.state = "all"
//                }
//
//                TxFilter{
//                    id: sent
//                    label: "SENT"
//                    onClicked: parent.state = "sent"
//                }
//
//                TxFilter{
//                    id: received
//                    label: "RECEIVED"
//                    onClicked: parent.state = "received"
//                }
//
//                TxFilter{
//                    id: in_progress
//                    label: "IN PROGRESS"
//                    onClicked: parent.state = "in_progress"
//                }
//
//                states: [
//                    State {
//                        name: "all"
//                        PropertyChanges {target: all; state: "active"}
//                    },
//                    State {
//                        name: "sent"
//                        PropertyChanges {target: sent; state: "active"}
//                    },
//                    State {
//                        name: "received"
//                        PropertyChanges {target: received; state: "active"}
//                    },
//                    State {
//                        name: "in_progress"
//                        PropertyChanges {target: in_progress; state: "active"}
//                    }
//                ]
//            }
        }

        Rectangle {
            anchors.fill: parent;
            anchors.topMargin: 394+46-33

            color: "#0a344d"
        }

        CustomTableView {

            id: tx_view

            anchors.fill: parent;
            anchors.topMargin: 394-33
            Layout.bottomMargin: 9

            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false

            TableViewColumn {
                role: "income"
                width: 40
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    clip:true

                    SvgImage {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: styleData.value ? "qrc:///assets/icon-received.svg" : "qrc:///assets/icon-sent.svg"
                    }
                }
            }

            TableViewColumn {
                role: "date"
                title: qsTr("Date | Time")
                width: 160 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideRight

                movable: false
            }

            TableViewColumn {
                role: "displayName"
                title: qsTr("Recipient / Sender ID")
                width: 260 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideMiddle

                movable: false
                delegate: Item {
                    anchors.fill: parent
                    clip:true
                    property string tooltip_text: (walletViewModel.tx[styleData.row] ? walletViewModel.tx[styleData.row].user : "")
                    SFText {
                        font.pixelSize: 12
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 20
                        elide: Text.ElideMiddle
                        anchors.verticalCenter: parent.verticalCenter
                        text: styleData.value
                        color: Style.white

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            acceptedButtons: Qt.NoButton
                            hoverEnabled: true
                        }

                        ToolTip {
                            id: toolTip
                            visible: mouseArea.containsMouse
                            delay: 500
                            timeout: 4000
                            text: tooltip_text

                            contentItem: Text {
                                text: toolTip.text
                                font: toolTip.font
                                color: Style.white
                            }

                            background: Rectangle {
                                border.color: Style.white
                                opacity: 0
                            }
                        }
                    }
                }
            }

            TableViewColumn {
                role: "amount"
                title: qsTr("Amount, BEAM")
                width: 200 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {
                    anchors.fill: parent
                    
                    property bool income: (styleData.row >= 0) ? walletViewModel.tx[styleData.row].income : false

                    SFText {
                        anchors.leftMargin: 20
                        anchors.right: parent.right
                        anchors.left: parent.left
                        color: parent.income ? Style.bright_sky_blue : Style.heliotrope
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: "<font size='6'>" + (parent.income ? "+ " : "- ") + styleData.value + "</font>"
                        textFormat: Text.StyledText
                    }
                }
            }

            TableViewColumn {
                role: "change"
                title: qsTr("Change, BEAM")
                width: 200 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideRight

                movable: false
            }

            TableViewColumn {
                role: "status"
                title: qsTr("Status")
                width: 96 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    clip:true

                    SFText {
                        font.pixelSize: 12
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 20
                        color: {
                            if(styleData.value === "sent")
                                Style.heliotrope
                            else if(styleData.value === "received")
                                Style.bright_sky_blue
                            else Style.white
                        }
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: styleData.value
                    }
                }
            }

            model: walletViewModel.tx

            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                SFText {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    font.pixelSize: 12
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            ContextMenu {
                id: txContextMenu
                property TxObject transaction
                property int index;
                Action {
                    text: qsTr("copy address")
                    icon.source: "qrc:///assets/icon-copy.svg"
                    onTriggered: {
                        if (!!txContextMenu.transaction)
                        {
                            addressBookViewModel.copyToClipboard(txContextMenu.transaction.user);
                        }
                    }
                }
                Action {
                    text: qsTr("cancel")
                    onTriggered: {
                       walletViewModel.cancelTx(txContextMenu.index);
                    }
                    enabled: !!txContextMenu.transaction && txContextMenu.transaction.canCancel
                    icon.source: "qrc:///assets/icon-cancel.svg"
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

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: {
                        if (mouse.button === Qt.RightButton && styleData.row !== undefined && styleData.row >=0)
                        {
                            txContextMenu.index = styleData.row;
                            txContextMenu.transaction = walletViewModel.tx[styleData.row];
                            txContextMenu.popup();
                        }
                    }
                }
            }

            itemDelegate: TableItem {
                text: styleData.value
                elide: styleData.elideMode
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
    
    Component.onCompleted: {
        if (root.toSend) {
            root.state = "send"
            root.toSend = false
        }
    }

    states: [
        State {
            name: "wallet"
        },

        State {
            name: "send"
            PropertyChanges {target: wallet_layout; visible: false}
            PropertyChanges {target: send_layout; visible: true}
            PropertyChanges {target: senderAddrCombo; currentIndex: -1}
            PropertyChanges {target: receiverAddrCombo; currentIndex: -1}
            PropertyChanges {target: amount_input; text: ""}
            PropertyChanges {target: mils_amount_input; text: ""}
            PropertyChanges {target: mils_fee_input; text: ""}
             StateChangeScript {
                script: senderAddrCombo.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    ]
}
