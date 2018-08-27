import QtQuick 2.6
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
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
    /// Receive layout //////////////////////////////////////////
    /////////////////////////////////////////////////////////////

    Item {
        id: receive_layout
        visible: false
        anchors.fill: parent
        anchors.topMargin: 73
        anchors.bottomMargin: 30
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 30

            SFText {
                Layout.alignment: Qt.AlignHCenter
                Layout.minimumHeight: 20
                font.pixelSize: 18
                font.weight: Font.Bold
                color: Style.white
                text: "Receive money"
            }

            SFText {
                font.pixelSize: 14
                Layout.minimumHeight: 16
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("My address")
            }

            SFTextInput {
                id: myAddressID
                Layout.fillWidth: true
                font.pixelSize: 14
                Layout.minimumHeight: 20
                color: Style.disable_text_color
                readOnly: true
                activeFocusOnTab: false
                //text: viewModel.newOwnAddress.walletID
                text: viewModel.newReceiverAddr
            }

            SFText {
                font.pixelSize: 14
                Layout.minimumHeight: 16
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Name")
            }

            SFTextInput {
                id: myAddressName
                Layout.fillWidth: true
                font.pixelSize: 14
                Layout.minimumHeight: 20
                color: Style.white
                //text: viewModel.newReceiverName
            }

            Binding {
                target: viewModel
                property: "newReceiverName"
                value: myAddressName.text
            }

            SFText {
                Layout.alignment: Qt.AlignHCenter
                Layout.minimumHeight: 16
                font.pixelSize: 14
                font.weight: Font.Bold
                color: Style.white
                text: "Send this address to the sender over an external secure channel"
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.minimumHeight: 40

                spacing: 19

                CustomButton {
                    text: "cancel"
                    height: 38
                    width: 122
                    onClicked: root.state = "wallet";
                }

                CustomButton {
                    text: "ok"
                    height: 38
                    width: 122
                    onClicked: {
                        viewModel.saveNewAddress();
                        root.state = "wallet";
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
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
                            text: qsTr("Send To:")
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
                            text: qsTr("Comment")
                        }

                        SFTextInput {
                            id: comment_input
                            Layout.fillWidth: true

                            font.pixelSize: 14
                            color: Style.white

                            // TODO: here should be proper validator (max text length 200)
                            selectByMouse: true
                        }

                        Binding {
                            target: viewModel
                            property: "comment"
                            value: comment_input.text
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
                        
                        FeeSlider {
                            id: feeSlider
                            Layout.fillWidth: true

                            to: 0.000010
                            stepSize: 0.000001
                            value: 0.0
                        }

                        Binding {
                            target: viewModel
                            property: "feeMils"
                            value: feeSlider.value
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 30
                            Layout.minimumHeight: 96

                            Rectangle {
                                anchors.fill: parent
                                height: 96
                                radius: 10
                                color: Style.marine

                                RowLayout {
                                    anchors.fill: parent
                                    readonly property int margin: 15
                                    anchors.leftMargin: margin
                                    anchors.rightMargin: margin
                                    spacing: margin

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignCenter
                                        height: childrenRect.height

                                        ColumnLayout {
                                            width: parent.width
                                            spacing: 10

                                            SFText {
                                                Layout.alignment: Qt.AlignHCenter
                                                font.pixelSize: 18
                                                font.weight: Font.Bold
                                                color: Style.bluey_grey
                                                text: qsTr("Remaining")
                                            }

                                            RowLayout
                                            {
                                                Layout.alignment: Qt.AlignHCenter
                                                spacing: 6
                                                clip: true

                                                SFText {
                                                    font.pixelSize: 24
                                                    font.weight: Font.ExtraLight
                                                    color: Style.bluey_grey
                                                    text: viewModel.actualAvailable
                                                }

                                                // TODO(alex.starun): change to BEAM icon
                                                SFText {
                                                    font.pixelSize: 24
                                                    font.weight: Font.ExtraLight
                                                    color: Style.bluey_grey
                                                    text: "B"
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        id: separator
                                        Layout.fillHeight: true
                                        Layout.topMargin: 10
                                        Layout.bottomMargin: 10
                                        width: 1
                                        color: Style.bluey_grey
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignCenter
                                        height: childrenRect.height

                                        ColumnLayout {
                                            width: parent.width
                                            spacing: 10

                                            SFText {
                                                Layout.alignment: Qt.AlignHCenter
                                                font.pixelSize: 18
                                                font.weight: Font.Bold
                                                color: Style.bluey_grey
                                                text: qsTr("Change")
                                            }

                                            RowLayout
                                            {
                                                Layout.alignment: Qt.AlignHCenter
                                                spacing: 6
                                                clip: true

                                                SFText {
                                                    font.pixelSize: 24
                                                    font.weight: Font.ExtraLight
                                                    color: Style.bluey_grey
                                                    text: viewModel.change
                                                }

                                                // TODO(alex.starun): change to BEAM icon
                                                SFText {
                                                    font.pixelSize: 24
                                                    font.weight: Font.ExtraLight
                                                    color: Style.bluey_grey
                                                    text: "B"
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                height: 96
                                radius: 10
                                color: "white"
                                opacity: 0.1
                            }
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

        Row{
            anchors.top: parent.top
            anchors.right: parent.right
            spacing: 19

            CustomButton {
                palette.button: Style.bright_sky_blue
                palette.buttonText: Style.marine
                height: 38
                width: 186
                icon.source: "qrc:///assets/icon-receive-blue.svg"
                icon.height: 16
                icon.width: 16
                text: qsTr("receive money")

                onClicked: {
                    viewModel.generateNewAddress();
                    root.state = "receive"
                }
            }

            CustomButton {
                palette.button: Style.heliotrope
                palette.buttonText: Style.marine
                icon.source: "qrc:///assets/icon-send-blue.svg"
                icon.height: 16
                icon.width: 16
                height: 38
                width: 169
                text: qsTr("send money")

                onClicked: root.state = "send"
            }

            
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
                role: "displayName"
                title: qsTr("Recipient / Sender ID")
                width: 260 * (parent.width - 40 - 20) / 916
                elideMode: Text.ElideMiddle

                movable: false
                delegate: Item {
                    anchors.fill: parent
                    clip:true
                    property string tooltip_text: (viewModel.tx[styleData.row] ? viewModel.tx[styleData.row].user : "")
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
            PropertyChanges {target: receiverAddrCombo; currentIndex: -1}
            PropertyChanges {target: amount_input; text: ""}
             StateChangeScript {
                script: receiverAddrCombo.forceActiveFocus(Qt.TabFocusReason);
            }
        },

        State {
            name: "receive"
            PropertyChanges {target: wallet_layout; visible: false}
            PropertyChanges {target: receive_layout; visible: true}
            PropertyChanges {target: myAddressName; text: ""}
        }
    ]
}
