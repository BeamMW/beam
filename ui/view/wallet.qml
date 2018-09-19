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

    ConfirmationDialog {
        id: invalidAddressDialog
        okButtonText: "got it"
    }

    ConfirmationDialog {
        id: deleteTransactionDialog
        okButtonText: "delete"
    }

    SFText {
        font.pixelSize: 36
        color: Style.white
        text: "Wallet"
    }

    Item {
        id: status_bar
        x: 5
        y: 53
        property string status: {
             if (viewModel.isisFailedStatus)
                "error"
             else if (viewModel.isOfflineStatus)
                "offline"
             else if(viewModel.isSyncInProgress)
                "updating"
             else
                "online"
        }

        state: "offline"

        property int indicator_radius: 5
        property Item indicator: online_indicator
        property string error_msg: viewModel.walletStatusErrorMsg

        Item {
            id: online_indicator
            anchors.top: parent.top
            anchors.left: parent.left
            width: childrenRect.width

            property color color: Style.bright_teal
            property int radius: status_bar.indicator_radius

            Rectangle {
                id: online_rect
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.leftMargin: 0
                anchors.topMargin: 2

                width: status_bar.indicator_radius * 2
                height: status_bar.indicator_radius * 2
                radius: status_bar.indicator_radius
                color: parent.color
            }

            DropShadow {
                anchors.fill: online_rect
                radius: 5
                samples: 9
                source: online_rect
                color: parent.color
            }
        }

        Item {
            id: offline_indicator
            anchors.top: parent.top
            anchors.left: parent.left
            width: childrenRect.width

            property color color: Style.bluey_grey
            property int radius: status_bar.indicator_radius


            Rectangle {
                id: offline_rect
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.leftMargin: 0
                anchors.topMargin: 2

                color: "transparent"
                width: parent.radius * 2
                height: parent.radius * 2
                radius: parent.radius
                border.color: parent.color
                border.width: 1
            }
        }

        Item {
            id: update_indicator
            anchors.top: parent.top
            anchors.left: parent.left
            visible: false

            property color color: Style.bright_teal
            property int circle_line_width: 2
            property int animation_duration: 2000

            width: 2 * status_bar.indicator_radius + circle_line_width
            height: 2 * status_bar.indicator_radius + circle_line_width

            Canvas {
                id: canvas_
                anchors.fill: parent
                onPaint: {
                    var context = getContext("2d");
                    context.arc(width/2, height/2, width/2 - parent.circle_line_width, 0, 1.6 * Math.PI);
                    context.strokeStyle = parent.color;
                    context.lineWidth = parent.circle_line_width;
                    context.stroke();
                }
            }

            RotationAnimator {
                target: update_indicator
                from: 0
                to: 360
                duration: update_indicator.animation_duration
                running: update_indicator.visible
                loops: Animation.Infinite
            }
        }

        SFText {
            id: status_text
            anchors.top: parent.top
            anchors.left: parent.indicator.right
            anchors.leftMargin: 5
            anchors.topMargin: -3
            color: Style.bluey_grey
            font.pixelSize: 14
        }
        SFText {
            id: progressText
            anchors.top: parent.top
            anchors.left: status_text.right
            anchors.leftMargin: 5
            anchors.topMargin: -3
            color: Style.bluey_grey
            font.pixelSize: 14
            text: "(" + viewModel.nodeSyncProgress + "%)"
            visible: viewModel.nodeSyncProgress > 0 && update_indicator.visible
        }

        states: [
            State {
                name: "online"
                when: (status_bar.status === "online")
                PropertyChanges {target: status_text; text: qsTr("online") + viewModel.branchName}
                PropertyChanges {target: status_bar; indicator: online_indicator}
                PropertyChanges {target: online_indicator; visible: true}
                PropertyChanges {target: offline_indicator; visible: false}
                PropertyChanges {target: update_indicator; visible: false}
            },
            State {
                name: "offline"
                when: (status_bar.status === "offline")
                PropertyChanges {target: status_text; text: qsTr("offline") + viewModel.branchName}
                PropertyChanges {target: status_bar; indicator: offline_indicator}
                PropertyChanges {target: online_indicator; visible: false}
                PropertyChanges {target: offline_indicator; visible: true}
                PropertyChanges {target: update_indicator; visible: false}
            },
            State {
                name: "updating"
                when: (status_bar.status === "updating")
                PropertyChanges {target: status_text; text: qsTr("updating...") + viewModel.branchName}
                PropertyChanges {target: status_bar; indicator: update_indicator}
                PropertyChanges {target: online_indicator; color: "red"}
                PropertyChanges {target: online_indicator; visible: false}
                PropertyChanges {target: offline_indicator; visible: false}
                PropertyChanges {target: update_indicator; visible: true}
            },
            State {
                name: "error"
                when: (status_bar.status === "error")
                PropertyChanges {target: status_bar; indicator: online_indicator}
                PropertyChanges {target: status_text; text: status_bar.error_msg + viewModel.branchName}
                PropertyChanges {target: status_text; color: "red"}
                PropertyChanges {target: online_indicator; color: "red"}
                PropertyChanges {target: online_indicator; visible: true}
                PropertyChanges {target: offline_indicator; visible: false}
                PropertyChanges {target: update_indicator; visible: false}
            }
        ]
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
                font.styleName: "Bold"
                color: Style.white
                text: "Receive Beam"
            }

            SFText {
                font.pixelSize: 14
                Layout.minimumHeight: 16
                font.styleName: "Bold"
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
                font.styleName: "Bold"
                color: Style.white
                text: qsTr("Comment")
            }

            SFTextInput {
                id: myAddressName
                Layout.fillWidth: true
                font.pixelSize: 14
                Layout.minimumHeight: 20
                color: Style.white
                focus: true
                text: viewModel.newReceiverName
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
                font.styleName: "Bold"
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
                    palette.buttonText: Style.white
                    onClicked: root.state = "wallet";
                }

                CustomButton {
                    text: "Copy && Close"
                    height: 38
                    width: 162
                    palette.buttonText: Style.white
                    icon.source: "qrc:///assets/icon-copy.svg"
                    icon.width: 16
                    icon.height: 16
                    onClicked: {
                        viewModel.copyToClipboard(myAddressID.text);
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
                font.styleName: "Bold"
                color: Style.white
                text: "Send Beam"
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
                            font.styleName: "Bold"
                            color: Style.white
                            text: qsTr("Send To:")
                        }

                        SFTextInput {
                            Layout.fillWidth: true

                            id: receiverAddrInput
                            font.pixelSize: 14
                            color: Style.white
                            text: viewModel.receiverAddr

                            validator: RegExpValidator { regExp: /[0-9a-fA-F]{1,64}/ }
                            selectByMouse: true

                            placeholderText: qsTr("Please specify contact")

                            onTextChanged : {
                                receiverAddressError.visible = receiverAddrInput.text.length > 0 && !viewModel.isValidReceiverAddress(receiverAddrInput.text)
                            }
                        }

                        SFText {
                            Layout.alignment: Qt.AlignTop
                            id: receiverAddressError
                            color: Style.validator_color
                            font.pixelSize: 10
                            text: qsTr("Invalid address")
                            visible: false
                        }

                        SFText {
                            id: receiverName
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"
                        }

                        Binding {
                            target: viewModel
                            property: "receiverAddr"
                            value: receiverAddrInput.text
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
                            font.styleName: "Bold"
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
                                    font.styleName: "Light"
                                    color: Style.heliotrope

                                    text: viewModel.sendAmount

                                    validator: RegExpValidator { regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,5}[1-9])?$/ }
                                    selectByMouse: true
                                }

                                Item {
                                    Layout.minimumHeight: 16
                                    Layout.fillWidth: true

                                    SFText {
                                        text: "Maximum available amount is " + viewModel.available + " B"
                                        color: Style.validator_color
                                        font.pixelSize: 14
                                        font.styleName: "Italic"
                                        visible: !viewModel.isEnoughMoney
                                    }
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
                            font.styleName: "Bold"
                            color: Style.white
                            text: qsTr("Comment")
                        }

                        SFTextInput {
                            id: comment_input
                            Layout.fillWidth: true

                            font.pixelSize: 14
                            color: Style.white

                            maximumLength: 200
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
                            font.styleName: "Bold"
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
                            height: 96

                            Item {
                                anchors.fill: parent

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
                                                font.styleName: "Bold"
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
                                                    font.styleName: "Light"
                                                    color: Style.bluey_grey
                                                    text: viewModel.actualAvailable
                                                }

                                                SvgImage {
                                                    Layout.topMargin: 4
                                                    sourceSize: Qt.size(16, 24)
                                                    source: "qrc:///assets/b-grey.svg"
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
                                                font.styleName: "Bold"
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
                                                    font.styleName: "Light"
                                                    color: Style.bluey_grey
                                                    text: viewModel.change
                                                }

                                                SvgImage {
                                                    Layout.topMargin: 4
                                                    sourceSize: Qt.size(16, 24)
                                                    source: "qrc:///assets/b-grey.svg"
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: Style.white
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
                    width: 122
                    text: qsTr("send")
                    palette.buttonText: Style.marine
                    palette.button: Style.heliotrope
                    icon.source: "qrc:///assets/icon-send.svg"
                    enabled: {viewModel.isEnoughMoney && amount_input.acceptableInput && receiverAddrInput.acceptableInput }
                    onClicked: {
                        if (viewModel.isValidReceiverAddress(viewModel.receiverAddr)) {
                            var message = "You are about to send %1 to address %2";
                            var beams = (viewModel.sendAmount*1 + viewModel.feeMils*1) + " " + qsTr("BEAM");

                            confirmationDialog.text = message.arg(beams).arg(viewModel.receiverAddr);
                            confirmationDialog.open();
                        } else {
                            var message = "Address %1 is invalid";
                            invalidAddressDialog.text = message.arg(viewModel.receiverAddr);
                            invalidAddressDialog.open();
                        }
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
                width: 122
                icon.source: "qrc:///assets/icon-receive-blue.svg"
                icon.height: 16
                icon.width: 16
                text: qsTr("receive")

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
                width: 122
                text: qsTr("send")

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
                    styleName: "Bold"
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

            id: transactionsView

            anchors.fill: parent;
            anchors.topMargin: 394-33
            Layout.bottomMargin: 9

            property int rowHeight: 69

            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false

            property int resizableWidth: parent.width - incomeColumn.width - actionsColumn.width - commentColumn.width

            TableViewColumn {
                id: incomeColumn
                role: "income"
                width: 40
                elideMode: Text.ElideRight
                movable: false
                resizable: false
                delegate: Item {

                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight

                        clip:true

                        SvgImage {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: styleData.value ? "qrc:///assets/icon-received.svg" : "qrc:///assets/icon-sent.svg"
                        }
                    }
                }
            }

            TableViewColumn {
                role: "date"
                title: qsTr("Date | Time")
                width: 160 * transactionsView.resizableWidth / 870
                elideMode: Text.ElideRight
                resizable: false
                movable: false
            }

            TableViewColumn {
                role: "displayName"
                title: qsTr("Recipient / Sender ID")
                width: 400 * transactionsView.resizableWidth / 870
                elideMode: Text.ElideMiddle
                resizable: false
                movable: false
                delegate: Item {
                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight
                        clip:true

                        SFText {
                            font.pixelSize: 14
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 20
                            elide: Text.ElideMiddle
                            anchors.verticalCenter: parent.verticalCenter
                            text: styleData.value
                            color: Style.white
                        }
                    }
                }
            }


            TableViewColumn {
                id: commentColumn
                role: "comment"
                title: qsTr("Comment")
                width: 100
                elideMode: Text.ElideRight
                movable: false
                resizable: false
                delegate: Item {
                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight
                        clip:true

                        SvgImage {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "qrc:///assets/icon-comment.svg"
                            visible: styleData.value !== null && styleData.value !== ""
                            ToolTip {
                                id: comment_tooltip
                                visible: mouseArea.containsMouse
                                delay: 500
                                timeout: 4000
                                text: styleData.value

                                contentItem: Text {
                                    text: comment_tooltip.text
                                    font: comment_tooltip.font
                                    color: Style.white
                                }

                                background: Rectangle {
                                    border.color: Style.white
                                    opacity: 0
                                }
                            }
                            MouseArea {
                                id: mouseArea
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                            }
                        }
                    }
                }
            }

            TableViewColumn {
                role: "amount"
                title: qsTr("Amount")
                width: 200 * transactionsView.resizableWidth / 870
                elideMode: Text.ElideRight
                movable: false
                resizable: false
                delegate: Item {
                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight
                        property bool income: (styleData.row >= 0) ? viewModel.transactions[styleData.row].income : false
                        SFText {
                            anchors.leftMargin: 24
                            anchors.right: parent.right
                            anchors.left: parent.left
                            color: parent.income ? Style.bright_sky_blue : Style.heliotrope
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                            text: "<font size='6'>" + (parent.income ? "+ " : "- ") + styleData.value + "</font>"
                            textFormat: Text.StyledText
                            font.styleName: "Light"
                        }
                    }
                }
            }

            TableViewColumn {
                role: "status"
                title: qsTr("Status")
                width: 110 * transactionsView.resizableWidth / 870
                elideMode: Text.ElideRight
                movable: false
                resizable: false
                delegate: Item {
                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight

                        clip:true

                        SFText {
                            font.pixelSize: 14
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
            }

            TableViewColumn {
                id: actionsColumn
                role: "status"
                title: ""
                width: 40
                movable: false
                resizable: false
                delegate: txActions
            }

            model: viewModel.transactions

            Component {
                id: txActions
                Item {
                    Item {
                        width: parent.width
                        height: transactionsView.rowHeight

                        Row{
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                        /*    CustomToolButton {
                                visible: styleData.row >= 0 && viewModel.transactions[styleData.row].canCancel
                                icon.source: "qrc:///assets/icon-cancel.svg"
                                ToolTip.text: qsTr("Cancel transaction")
                                onClicked: {
                                    viewModel.cancelTx(styleData.row);
                                }
                            }
                            */
                            CustomToolButton {
                                icon.source: "qrc:///assets/icon-actions.svg"
                                ToolTip.text: qsTr("Actions")
                                onClicked: {
                                    txContextMenu.index = styleData.row;
                                    txContextMenu.transaction = viewModel.transactions[styleData.row];
                                    txContextMenu.popup();
                                }
                            }
                        }
                    }
                }
            }

            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                SFText {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    font.pixelSize: 14
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            ContextMenu {
                id: txContextMenu
                modal: true
                dim: false
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
                Action {
                    text: qsTr("delete")
                    icon.source: "qrc:///assets/icon-delete.svg"
                    enabled: !!txContextMenu.transaction && txContextMenu.transaction.canDelete
                    onTriggered: {
                        deleteTransactionDialog.text = qsTr("The transaction will be deleted. This operation can not be undone");
                        deleteTransactionDialog.open();
                    }
                }
                Connections {
                    target: deleteTransactionDialog
                    onAccepted: {
                        viewModel.deleteTx(txContextMenu.index);
                    }
                }
            }

            rowDelegate: Item {
                height: transactionsView.rowHeight
                id: rowItem
                property bool collapsed: true

                width: parent.width

                Column {
                    id: rowColumn
                    width: parent.width
                    Rectangle {
                        height: transactionsView.rowHeight
                        width: parent.width
                        color: styleData.alternate ? "transparent" : Style.light_navy
                    }
                    Item {
                        id: txDetails
                        height: 0
                        visible: height > 0
                        width: parent.width
                        clip: true

                        property int maximumHeight: 200

                        Rectangle {
                            anchors.fill: parent
                            color: Style.bright_sky_blue
                            opacity: 0.1
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            GridLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: 30
                                columns: 2
                                columnSpacing: 44
                                rowSpacing: 14

                                SFText {
                                    font.pixelSize: 14
                                    color: Style.white
                                    text: qsTr("General transaction info")
                                    font.styleName: "Bold"
                                    Layout.columnSpan: 2
                                }

                                SFText {
                                    Layout.row: 1
                                    font.pixelSize: 14
                                    color: Style.bluey_grey
                                    text: qsTr("Sending address:")
                                }
                                SFText {
                                    font.pixelSize: 14
                                    color: Style.white
                                    text: {
                                        if(!!viewModel.transactions[styleData.row])
                                        {
                                            return viewModel.transactions[styleData.row].sendingAddress;
                                        }
                                        return "";
                                    }
                                }

                                SFText {
                                    Layout.row: 2
                                    font.pixelSize: 14
                                    color: Style.bluey_grey
                                    text: qsTr("Receiving address:")
                                }
                                SFText {
                                    font.pixelSize: 14
                                    color: Style.white
                                    text: {
                                        if(!!viewModel.transactions[styleData.row])
                                        {
                                            return viewModel.transactions[styleData.row].receivingAddress;
                                        }
                                        return "";
                                    }
                                }

                                SFText {
                                    Layout.row: 3
                                    font.pixelSize: 14
                                    color: Style.bluey_grey
                                    text: qsTr("Transaction fee:")
                                }
                                SFText {
                                    font.pixelSize: 14
                                    color: Style.white
                                    text:{
                                        if(!!viewModel.transactions[styleData.row])
                                        {
                                            return viewModel.transactions[styleData.row].fee;
                                        }
                                        return "";
                                    }
                                }

                                SFText {
                                   Layout.row: 4
                                    font.pixelSize: 14
                                    color: Style.bluey_grey
                                    text: qsTr("Comment:")
                                }
                                SFText {
                                    font.pixelSize: 14
                                    color: Style.white
                                    text: {
                                        if(!!viewModel.transactions[styleData.row])
                                        {
                                            return viewModel.transactions[styleData.row].comment;
                                        }
                                        return "";
                                    }
                                    font.styleName: "Italic"
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                            }
                        }
                    }
                }


                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: {
                        if (styleData.row === undefined 
                         || styleData.row < 0
                         || styleData.row >= viewModel.transactions.length)
                        {
                            return;
                        }
                        if (mouse.button === Qt.RightButton )
                        {
                            txContextMenu.index = styleData.row;
                            txContextMenu.transaction = viewModel.transactions[styleData.row];
                            txContextMenu.popup();
                        }
                        else if (mouse.button === Qt.LeftButton && !!viewModel.transactions[styleData.row])
                        {
                            if (parent.collapsed)
                            {
                                expand.start()
                            }
                            else 
                            {
                                collapse.start()
                            }
                            parent.collapsed = !parent.collapsed;
                        }
                    }
                }

                ParallelAnimation {
                    id: expand
                    running: false

                    property int expandDuration: 200

                    NumberAnimation {
                        target: rowItem
                        easing.type: Easing.Linear
                        property: "height"
                        to: transactionsView.rowHeight + txDetails.maximumHeight
                        duration: expand.expandDuration
                    }

                    NumberAnimation {
                        target: txDetails
                        easing.type: Easing.Linear
                        property: "height"
                        to: txDetails.maximumHeight
                        duration: expand.expandDuration
                    }
                }

                ParallelAnimation {
                    id: collapse
                    running: false

                    property int collapseDuration: 200

                    NumberAnimation {
                        target: rowItem
                        easing.type: Easing.Linear
                        property: "height"
                        to: transactionsView.rowHeight
                        duration: collapse.collapseDuration
                    }

                    NumberAnimation {
                        target: txDetails
                        easing.type: Easing.Linear
                        property: "height"
                        to: 0
                        duration: collapse.collapseDuration
                    }
                }
            }

            itemDelegate: Item {
                Item {
                    width: parent.width
                    height: transactionsView.rowHeight
                    TableItem {
                        text: styleData.value
                        elide: styleData.elideMode
                    }
                }
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
            PropertyChanges {target: amount_input; text: ""}
            StateChangeScript {
                script: receiverAddrInput.forceActiveFocus(Qt.TabFocusReason);
            }
        },

        State {
            name: "receive"
            PropertyChanges {target: wallet_layout; visible: false}
            PropertyChanges {target: receive_layout; visible: true}
            PropertyChanges {target: myAddressName; text: ""}
            StateChangeScript {
                script: myAddressName.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    ]
}
