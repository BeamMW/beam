import QtQuick 2.6
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Controls 1.4 as QC14
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

Item {
    id: root
    anchors.fill: parent

	WalletViewModel {id: viewModel}
	AddressBookViewModel {id: addressBookViewModel}

    property bool toSend: false

    state: "wallet"

    ConfirmationDialog {
        id: confirmationDialog
        okButtonText: "send"
        onAccepted: {
            viewModel.sendMoney()
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

        color: (viewModel.isSyncInProgress == false) ? Style.bright_teal : "red"
    }

    DropShadow {
        anchors.fill: user_led
        radius: 5
        samples: 9
        color: (viewModel.isSyncInProgress == false) ? Style.bright_teal : "red"
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
            if(viewModel.syncProgress < 0)
                "Last update time: " + viewModel.syncTime + " (<a href=\"update\">update</a>)"
            else
                "Updating, please wait... [" + viewModel.syncProgress + "%]"
        }

        onLinkActivated: {
            if(link == "update")
            {
                viewModel.syncWithNode()
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

    Item {
        id: send_layout
        anchors.fill: parent
        anchors.topMargin: 73
        anchors.bottomMargin: 30

        ColumnLayout {
            anchors.fill: parent

            spacing: 30

            SFText {
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 18
                font.weight: Font.Bold
                color: Style.white
                text: "Send money"
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 50

                spacing: 70

                Item {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    height: childrenRect.height

                    ColumnLayout {
                        width: parent.width

                        spacing: 12

                        SFText {
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("My address")
                        }
                        
                        AddressComboBox {
                            Layout.fillWidth: true

                            id: senderAddrCombo
                            editable: true
                            model: addressBookViewModel.ownAddresses
                            editText: viewModel.senderAddr
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
                            id: senderName
                            color: Style.white
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }  
                    }

                }

                Item {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    height: childrenRect.height

                    ColumnLayout {
                        width: parent.width

                        spacing: 12

                        SFText {
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
                                    Layout.fillWidth: true

                                    id: amount_input

                                    font.pixelSize: 36
                                    color: Style.heliotrope

                                    text: viewModel.sendAmount

                                    // TODO: here should be proper validator
                                    // validator: DoubleValidator{bottom: 0; top: 210000000;}
                                    selectByMouse: true
                                }

                                Binding {
                                    target: viewModel
                                    property: "sendAmount"
                                    value: amount_input.text
                                }   
                            }

                            SFText {
                                font.pixelSize: 24
                                color: Style.white
                                text: qsTr("BEAM")
                            }
                        } 
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                spacing: 70

                Item {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    height: childrenRect.height

                    ColumnLayout {
                        width: parent.width

                        spacing: 12

                        SFText {
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Peer address")
                        }
                        
                        AddressComboBox {
                            Layout.fillWidth: true

                            id: receiverAddrCombo
                            editable: true
                            editText: viewModel.receiverAddr
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
                            id: receiverName
                            color: Style.white
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }

                        Binding {
                            target: viewModel
                            property: "senderAddr"
                            value: senderAddrCombo.editText
                        }

                        Binding {
                            target: viewModel
                            property: "receiverAddr"
                            value: receiverAddrCombo.editText
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    height: childrenRect.height

                    ColumnLayout {
                        width: parent.width

                        spacing: 12

                        SFText {
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Transaction fee")
                        }

                        SFText {
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: Style.white
                            text: feeSlider.value + " BEAM"
                        }

                        QC14.Slider {
                            id: feeSlider
                            Layout.fillWidth: true

                            maximumValue: 0.000002
                            stepSize: 0.000001
                            value: 0.0
                            tickmarksEnabled: true

                            style: SliderStyle {

                                groove: Rectangle {
                                    implicitWidth: 200
                                    implicitHeight: 4
                                    color: "white"
                                    radius: 10
                                    opacity: 0.1
                                }

                                handle: Rectangle {
                                    anchors.centerIn: parent
                                    implicitWidth: 20
                                    implicitHeight: 20
                                    radius: 10

                                    Rectangle {
                                        id: shadow
                                        anchors.fill: parent
                                        color: Style.bright_teal
                                        radius: parent.radius
                                    }

                                    DropShadow {
                                        anchors.fill: shadow
                                        radius: 5
                                        samples: 9
                                        color: shadow.color
                                        source: shadow
                                    }
                                }
                            }
                        }

                        Binding {
                            target: viewModel
                            property: "feeMils"
                            value: feeSlider.value
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                height: childrenRect.height

                ColumnLayout {
                    width: parent.width

                    spacing: 12

                    SFText {
                        font.pixelSize: 18
                        font.weight: Font.Bold
                       
                        color: Style.white
                        text: qsTr("Available")
                    }

                    Row
                    {
                        spacing: 6

                        SFText {
                            font.pixelSize: 36
                            font.weight: Font.ExtraLight
                            color: Style.bright_teal

                            text: viewModel.actualAvailable
                        }

                        SFText {
                            font.pixelSize: 24
                            font.weight: Font.ExtraLight
                            color: Style.bright_teal

                            text: "BEAM"
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 3
                        }
                    }
                }
                
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30

                spacing: 30

                CustomButton {
                    width: 122
                    text: qsTr("cancel")
                    palette.buttonText: Style.white
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
                        var beams = (viewModel.sendAmount*1 + viewModel.feeMils*1) + " " + qsTr("BEAM");

                        confirmationDialog.text = message.arg(beams).arg(viewModel.receiverAddr);
                        confirmationDialog.open();                        
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
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
                    
                    value: viewModel.available
                }

                SecondaryPanel {
                    Layout.minimumWidth: 350
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    title: qsTr("Unconfirmed")
                    amountColor: Style.white
                    value: viewModel.unconfirmed
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
                role: "user"
                title: qsTr("Recipient / Sender ID")
                width: 260 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideMiddle

                movable: false
            }

            TableViewColumn {
                role: "amount"
                title: qsTr("Amount, BEAM")
                width: 200 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {
                    anchors.fill: parent
                    
                    property bool income: (styleData.row >= 0) ? viewModel.tx[styleData.row].income : false

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

            model: viewModel.tx

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
                       viewModel.cancelTx(txContextMenu.index);
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
                            txContextMenu.transaction = viewModel.tx[styleData.row];
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
            // PropertyChanges {target: mils_amount_input; text: ""}
            // PropertyChanges {target: mils_fee_input; text: ""}
             StateChangeScript {
                script: senderAddrCombo.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    ]
}
