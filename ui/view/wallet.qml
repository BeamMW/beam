import QtQuick 2.11
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

    WalletViewModel {
        id: viewModel
        onSendMoneyVerified: {
            walletView.enabled = true
            walletView.pop();
        }

        onCantSendToExpired: {
            walletView.enabled = true;
            cantSendToExpiredDialog.open();
        }

        onNewAddressFailed: {
            walletView.enabled = true;
            newAddressFailedDialog.open();
        }
    }

    property bool toSend: false

    Dialog {
        id: cantSendToExpiredDialog

        modal: true

        width: 300
        height: 160

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        visible: false
        
        background: Rectangle {
            radius: 10
            color: Style.background_second
            anchors.fill: parent
        }

        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 30

            spacing: 40

            SFText {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Can't send to the expired address."
                text: qsTrId("cant-send-to-expired-message")
                color: Style.content_main
                font.pixelSize: 14
                font.styleName: "Bold"; font.weight: Font.Bold
            }

            PrimaryButton {
                //% "Ok"
                text: qsTrId("general-ok")
                anchors.horizontalCenter: parent.horizontalCenter
                icon.source: "qrc:/assets/icon-done.svg"
                onClicked: cantSendToExpiredDialog.close()
            }
        }
    }

    Dialog {
        id: newAddressFailedDialog

        modal: true

        width: 400
        height: 160

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        visible: false
        
        background: Rectangle {
            radius: 10
            color: Style.background_second
            anchors.fill: parent
        }

        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 30

            spacing: 40

            SFText {
                width: parent.width
                // text: qsTr("You cannot generate new address. Your wallet doesn't have a master key.")
                //% "You cannot generate new address. Your wallet doesn't have a master key."
                text: qsTrId("can-not-generate-new-address-message")
                color: Style.content_main
                font.pixelSize: 14
                font.styleName: "Bold"; font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }

            PrimaryButton {
                // text: qsTr("ok")
                //% "Ok"
                text: qsTrId("general-ok")
                anchors.horizontalCenter: parent.horizontalCenter
                icon.source: "qrc:/assets/icon-done.svg"
                onClicked: newAddressFailedDialog.close()
            }
        }
    }

    ConfirmationDialog {
        id: confirmationDialog
        okButtonColor: Style.accent_outgoing
        //% "Send"
        okButtonText: qsTrId("general-send")
        okButtonIconSource: "qrc:/assets/icon-send-blue.svg"
        cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
        okButtonEnable: viewModel.isPasswordReqiredToSpendMoney() ? requirePasswordInput.text.length : true

        property alias addressText: addressLabel.text
        property alias amountText: amountLabel.text
        property alias feeText: feeLabel.text
        property Item defaultFocusItem: viewModel.isPasswordReqiredToSpendMoney() ? requirePasswordInput : cancelButton

        function confirmationHandler() {
            if (viewModel.isPasswordReqiredToSpendMoney()) {
                if (requirePasswordInput.text.length == 0) {
                    requirePasswordInput.forceActiveFocus(Qt.TabFocusReason);
                    return;
                }
                if (!viewModel.isPasswordValid(requirePasswordInput.text)) {
                    requirePasswordInput.forceActiveFocus(Qt.TabFocusReason);
                    //% "Invalid password provided"
                    requirePasswordError.text = qsTrId("general-pwd-invalid");
                    return;
                }
            }
            accepted();
            close();
        }

        function openHandler() {
            var defaultFocusItem = viewModel.isPasswordReqiredToSpendMoney() ? requirePasswordInput : cancelButton;
            defaultFocusItem.forceActiveFocus(Qt.TabFocusReason);
        }

        function requirePasswordInputKeyEnter() {
            okButton.forceActiveFocus(Qt.TabFocusReason);
            okButton.clicked();
        }

        contentItem: Item {
            id: sendConfirmationContent
            ColumnLayout {
                anchors.fill: parent
                spacing: 30

                SFText {
                    id: title
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 21
                    Layout.leftMargin: 68
                    Layout.rightMargin: 68
                    Layout.topMargin: 14
                    font.pixelSize: 18
                    font.styleName: "Bold";
                    font.weight: Font.Bold
                    color: Style.content_main
                    //% "Confirm transaction details"
                    text: qsTrId("send-confirmation-title")
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    columnSpacing: 14
                    rowSpacing: 12
                    columns: 2
                    rows: 5

                    SFText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 16
                        font.pixelSize: 14
                        color: Style.content_disabled
                        //% "Recipient"
                        text: qsTrId("send-confirmation-recipient-label") + ":"
                        verticalAlignment: Text.AlignTop
                    }

                    SFText {
                        id: addressLabel
                        Layout.fillWidth: true
                        Layout.maximumWidth: 290
                        Layout.minimumHeight: 16
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        font.pixelSize: 14
                        color: Style.content_main
                    }

                    SFText {
                        Layout.row: 2
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 16
                        Layout.bottomMargin: 3
                        font.pixelSize: 14
                        color: Style.content_disabled
                        //% "Amount"
                        text: qsTrId("general-amount") + ":"
                        verticalAlignment: Text.AlignBottom
                    }

                    SFText {
                        id: amountLabel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 29
                        font.pixelSize: 24
                        color: Style.accent_outgoing
                        verticalAlignment: Text.AlignBottom
                    }

                    SFText {
                        Layout.row: 3
                        Layout.fillWidth: true
                        Layout.minimumHeight: 16
                        font.pixelSize: 14
                        color: Style.content_disabled
                        //% "Transaction fee"
                        text: qsTrId("general-fee") + ":"
                    }

                    SFText {
                        id: feeLabel
                        Layout.fillWidth: true
                        Layout.minimumHeight: 16
                        font.pixelSize: 14
                        color: Style.content_main
                    }

                    SFText {
                        id: requirePasswordLabel
                        visible: viewModel.isPasswordReqiredToSpendMoney()
                        Layout.row: 4
                        Layout.columnSpan: 2
                        Layout.topMargin: 50
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.minimumHeight: 16
                        font.pixelSize: 14
                        color: Style.content_main
                        //% "To broadcast your transaction please enter your password"
                        text: qsTrId("send-confirmation-pwd-require-message")
                    }

                    SFTextInput {
                        id: requirePasswordInput
                        visible: viewModel.isPasswordReqiredToSpendMoney()
                        Layout.row: 5
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        focus: true
                        activeFocusOnTab: true
                        font.pixelSize: 14
                        color: Style.content_main
                        echoMode: TextInput.Password
                        onAccepted: confirmationDialog.requirePasswordInputKeyEnter()
                        onTextChanged: if (requirePasswordError.text.length > 0) requirePasswordError.text = ""

                    }
                    SFText {
                        id: requirePasswordError
                        visible: viewModel.isPasswordReqiredToSpendMoney()
                        Layout.row: 6
                        Layout.columnSpan: 2
                        Layout.bottomMargin: 15
                        height: 16
                        width: parent.width
                        color: Style.validator_error
                        font.pixelSize: 14
                    }
                }
            }
        }

        onClosed: {
            requirePasswordInput.text = "";
        }

        onAccepted: {
            viewModel.sendMoney();
            walletView.enabled = false;
        }
    }

    ConfirmationDialog {
        id: invalidAddressDialog
        //% "Got it"
        okButtonText: qsTrId("invalid-addr-got-it-button")
    }

    ConfirmationDialog {
        id: deleteTransactionDialog
        //% "Delete"
        okButtonText: qsTrId("general-delete")
    }

    OpenExternalLinkConfirmation {
        id: exchangesList
    }   

    PaymentInfoDialog {
        id: paymentInfoDialog
        onTextCopied: function(text){
            viewModel.copyToClipboard(text);
        }
    }

    PaymentInfoItem {
        id: verifyInfo
    }

    PaymentInfoDialog {
        id: paymentInfoVerifyDialog
        shouldVerify: true
        
        model:verifyInfo 
        onTextCopied: function(text){
            viewModel.copyToClipboard(text);
        }
    }
    
    SFText {
        font.pixelSize: 36
        color: Style.content_main
        //% "Wallet"
        text: qsTrId("wallet-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    /////////////////////////////////////////////////////////////
    /// Receive layout //////////////////////////////////////////
    /////////////////////////////////////////////////////////////

    Component {
        id: receive_layout
        Item {
            
            property Item defaultFocusItem: myAddressName
            property bool isAddressCommentDuplicated: false

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 73
                anchors.bottomMargin: 30
                spacing: 30

                SFText {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 21
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.content_main
                    //% "Receive Beam"
                    text: qsTrId("wallet-receive-title")
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                        // TODO: find better solution, because it's bad
                        Layout.minimumHeight: 220
                        Column {
                            anchors.fill: parent
                            spacing: 10

                            SFText {
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "My address"
                                text: qsTrId("wallet-receive-my-addr-label")
                            }

                            SFTextInput {
                                id: myAddressID
                                width: parent.width
                                font.pixelSize: 14
                                color: Style.content_disabled
                                readOnly: true
                                activeFocusOnTab: false
                                text: viewModel.newReceiverAddr
                            }

                            Row {
                                spacing: 10
                                SFText {
                                    font.pixelSize: 14
                                    font.italic: true
                                    color: Style.content_main
                                    //% "Expires"
                                    text: qsTrId("wallet-receive-expires-label") + ":"
                                }
                                CustomComboBox {
                                    id: expiresControl
                                    width: 100
                                    height: 20
                                    anchors.top: parent.top
                                    anchors.topMargin: -3

                                    currentIndex: viewModel.expires

                                    Binding {
                                        target: viewModel
                                        property: "expires"
                                        value: expiresControl.currentIndex
                                    }

                                    model: [
                                        //% "24 hours"
                                        qsTrId("wallet-receive-expires-24"),
                                        //% "Never"
                                        qsTrId("wallet-receive-expires-never")
                                    ]
                                }
                            }

                            // Amount
                            ColumnLayout {
                                width: parent.width

                                SFText {
                                    font.pixelSize: 14
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    color: Style.content_main
                                    //% "Receive amount (optional)"
                                    text: qsTrId("receive-amount-label")
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    SFTextInput {
                                        Layout.fillWidth: true

                                        id: receiveAmountInput

                                        font.pixelSize: 36
                                        font.styleName: "Light"; font.weight: Font.Light
                                        color: Style.accent_outgoing

                                        property double amount: 0

                                        validator: RegExpValidator { regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,7}[1-9])?$/ }
                                        selectByMouse: true
                                    
                                        onTextChanged: {
                                            if (focus) {
                                                amount = text ? text : 0;
                                            }
                                        }

                                        onFocusChanged: {
                                            if (amount > 0) {
                                                text = amount.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128);
                                            }
                                        }
                                    }

                                    Binding {
                                        target: viewModel
                                        property: "amountForReceive"
                                        value: receiveAmountInput.amount
                                    }

                                    SFText {
                                        font.pixelSize: 24
                                        color: Style.content_main
                                        //% "BEAM"
                                        text: qsTrId("general-beam")
                                    }
                                }
                            }
                            // Comment
                            SFText {
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "Comment"
                                text: qsTrId("general-comment")
                            }

                            SFTextInput {
                                id: myAddressName
                                font.pixelSize: 14
                                width: parent.width
                                font.italic : isAddressCommentDuplicated
                                backgroundColor: isAddressCommentDuplicated ? Style.validator_error : Style.content_main
                                color: isAddressCommentDuplicated ? Style.validator_error : Style.content_main
                                focus: true
                                text: viewModel.newReceiverName
                                onTextEdited: {
                                    isAddressCommentDuplicated = viewModel.isAddressWithCommentExist(myAddressName.text);
                                    if (!isAddressCommentDuplicated) {
                                        viewModel.newReceiverName = myAddressName.text;
                                    }
                                }
                            }

                            SFText {
                                //% "Address with same comment already exist"
                                text: qsTrId("general-addr-comment-error")
                                color: Style.validator_error
                                font.pixelSize: 12
                                visible: isAddressCommentDuplicated
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        // TODO: find better solution, because it's bad
                        Layout.minimumHeight: 220
                        Column {
                            anchors.fill: parent
                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                fillMode: Image.Pad
                        
                                source: viewModel.newReceiverAddrQR
                            }
                            SFText {
                                anchors.horizontalCenter: parent.horizontalCenter
                                font.pixelSize: 14
                                font.italic: true
                                color: Style.content_main
                                //% "Scan to send"
                                text: qsTrId("wallet-receive-qr-label")
                            }
                        }
                    }
                }

                SFText {
                    Layout.topMargin: 15
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 16
                    font.pixelSize: 14
                    color: Style.content_main
                    //% "Send this address to the sender over an external secure channel"
                    text: qsTrId("wallet-receive-propogate-addr-message")
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 40

                    spacing: 19

                    CustomButton {
                        //% "Close"
                        text: qsTrId("general-close")
                        palette.buttonText: Style.content_main
                        icon.source: "qrc:/assets/icon-cancel-white.svg"
                        onClicked: {
                            walletView.pop();
                        }
                    }

                    CustomButton {
                        //% "Copy"
                        text: qsTrId("general-copy")
                        palette.buttonText: Style.content_opposite
                        icon.color: Style.content_opposite
                        palette.button: Style.active
                        icon.source: "qrc:/assets/icon-copy.svg"
                        onClicked: {
                            viewModel.copyToClipboard(myAddressID.text);
                        }
                    }
                }

                Component.onDestruction: {
                    // TODO: "Save" may be deleted in future, when we'll have editor for own addresses.
                    if (!isAddressCommentDuplicated) {
                        viewModel.saveNewAddress();
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    /////////////////////////////////////////////////////////////
    /// Send layout /////////////////////////////////////////////
    /////////////////////////////////////////////////////////////

    Component {
        id: send_layout
        Item {
            property Item defaultFocusItem: receiverAddrInput

            Component.onCompleted: {
                receiverAddrInput.text = "";
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 73
                anchors.bottomMargin: 30

                SFText {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.content_main
                    //% "Send Beam"
                    text: qsTrId("send-title")
                }

                Item {
                    Layout.fillHeight: true
                    Layout.minimumHeight: 10
                    Layout.maximumHeight: 30
                }

                RowLayout {
                    Layout.fillWidth: true
                    //Layout.topMargin: 50

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
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "Send To"
                                text: qsTrId("send-send-to-label") + ":"
                            }

                            SFTextInput {
                                Layout.fillWidth: true

                                id: receiverAddrInput
                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.receiverAddr

                                validator: RegExpValidator { regExp: /[0-9a-fA-F]{1,80}/ }
                                selectByMouse: true

                                //% "Please specify contact"
                                placeholderText: qsTrId("send-contact-placeholder")

                                onTextChanged : {
                                    receiverAddressError.visible = receiverAddrInput.text.length > 0 && !viewModel.isValidReceiverAddress(receiverAddrInput.text)
                                }
                            }

                            SFText {
                                Layout.alignment: Qt.AlignTop
                                id: receiverAddressError
                                color: Style.validator_error
                                font.pixelSize: 10
                                //% "Invalid address"
                                text: qsTrId("general-invalid-address")
                                visible: false
                            }

                            SFText {
                                id: receiverName
                                color: Style.content_main
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
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
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "Transaction amount"
                                text: qsTrId("send-amount-label")
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                SFTextInput {
                                    Layout.fillWidth: true

                                    id: amount_input

                                    font.pixelSize: 36
                                    font.styleName: "Light"; font.weight: Font.Light
                                    color: Style.accent_outgoing

                                    property double amount: 0

                                    validator: RegExpValidator { regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,7}[1-9])?$/ }
                                    selectByMouse: true
                                    
                                    onTextChanged: {
                                        if (focus) {
                                            amount = text ? text : 0;
                                        }
                                    }

                                    onFocusChanged: {
                                        if (amount > 0) {
                                            text = amount.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128);
                                        }
                                    }
                                }

                                Binding {
                                    target: viewModel
                                    property: "sendAmount"
                                    value: amount_input.amount
                                }

                                SFText {
                                    font.pixelSize: 24
                                    color: Style.content_main
                                    //% "BEAM"
                                    text: qsTrId("general-beam")
                                }
                            }
                            Item {
                                Layout.topMargin: -12
                                Layout.minimumHeight: 16
                                Layout.fillWidth: true

                                SFText {
                                    //% "Insufficient funds: you would need %1 to complete the transaction"
                                    text: qsTrId("send-founds-fail").arg(viewModel.amountMissingToSend)
                                    color: Style.validator_error
                                    font.pixelSize: 14
                                    fontSizeMode: Text.Fit
                                    minimumPixelSize: 10
                                    font.styleName: "Italic"
                                    width: parent.width
                                    visible: !viewModel.isEnoughMoney
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
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "Comment"
                                text: qsTrId("general-comment")
                            }

                            SFTextInput {
                                id: comment_input
                                Layout.fillWidth: true

                                font.pixelSize: 14
                                color: Style.content_main

                                maximumLength: 1024
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
                                font.styleName: "Bold"; font.weight: Font.Bold
                                color: Style.content_main
                                //% "Transaction fee"
                                text: qsTrId("general-fee")
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true

                                    SFTextInput {
                                        Layout.fillWidth: true
                                        id: fee_input

                                        font.pixelSize: 36
                                        font.styleName: "Light"; font.weight: Font.Light
                                        color: Style.accent_outgoing

                                        text: viewModel.defaultFeeInGroth.toLocaleString(Qt.locale(), 'f', -128)

                                        property int amount: viewModel.defaultFeeInGroth

                                        validator: IntValidator {bottom: viewModel.minimumFeeInGroth}
                                        maximumLength: 15
                                        selectByMouse: true
                                    
                                        onTextChanged: {
                                            if (focus) {
                                                amount = text ? text : 0;
                                            }
                                        }

                                        onFocusChanged: {
                                            if (amount >= 0) {
                                                // QLocale::FloatingPointShortest = -128
                                                text = focus ? amount : amount.toLocaleString(Qt.locale(), 'f', -128);
                                            }
                                        }
                                    }
                                }

                                SFText {
                                    font.pixelSize: 24
                                    color: Style.content_main
                                    //% "GROTH"
                                    text: qsTrId("send-curency-sub-name")
                                }
                            }

                            Item {
                                Layout.topMargin: -12
                                Layout.minimumHeight: 16
                                Layout.fillWidth: true

                                SFText {
                                    //% "The minimum fee is %1 GROTH"
                                    text: qsTrId("send-fee-fail").arg(viewModel.minimumFeeInGroth)
                                    color: Style.validator_error
                                    font.pixelSize: 14
                                    fontSizeMode: Text.Fit
                                    minimumPixelSize: 10
                                    font.styleName: "Italic"
                                    width: parent.width
                                    visible: fee_input.amount < viewModel.minimumFeeInGroth
                                }
                            }

                            Binding {
                                target: viewModel
                                property: "feeGrothes"
                                //value: feeSlider.value
                                value: fee_input.amount
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 20
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
                                                    font.styleName: "Bold"; font.weight: Font.Bold
                                                    color: Style.content_secondary
                                                    //% "Remaining"
                                                    text: qsTrId("send-remaining-label")
                                                }

                                                RowLayout
                                                {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    spacing: 6
                                                    clip: true

                                                    SFText {
                                                        font.pixelSize: 24
                                                        font.styleName: "Light"; font.weight: Font.Light
                                                        color: Style.content_secondary
                                                        text: viewModel.actualAvailable
                                                    }

                                                    SvgImage {
                                                        Layout.topMargin: 4
                                                        sourceSize: Qt.size(16, 24)
                                                        source: "qrc:/assets/b-grey.svg"
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
                                            color: Style.content_secondary
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
                                                    font.styleName: "Bold"; font.weight: Font.Bold
                                                    color: Style.content_secondary
                                                    //% "Change"
                                                    text: qsTrId("general-change")
                                                }

                                                RowLayout
                                                {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    spacing: 6
                                                    clip: true

                                                    SFText {
                                                        font.pixelSize: 24
                                                        font.styleName: "Light"; font.weight: Font.Light
                                                        color: Style.content_secondary
                                                        text: viewModel.change
                                                    }

                                                    SvgImage {
                                                        Layout.topMargin: 4
                                                        sourceSize: Qt.size(16, 24)
                                                        source: "qrc:/assets/b-grey.svg"
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

                Item {
                    Layout.fillHeight: true
                    Layout.minimumHeight: 10
                    Layout.maximumHeight: 30
                }

                Row {
                    Layout.alignment: Qt.AlignHCenter

                    spacing: 30

                    CustomButton {
                        //% "Back"
                        text: qsTrId("general-back")
                        icon.source: "qrc:/assets/icon-back.svg"
                        onClicked: {
                            walletView.pop();
                        }
                    }

                    CustomButton {
                        //% "Send"
                        text: qsTrId("general-send")
                        palette.buttonText: Style.content_opposite
                        palette.button: Style.accent_outgoing
                        icon.source: "qrc:/assets/icon-send-blue.svg"
                        enabled: {viewModel.isEnoughMoney && amount_input.amount > 0 && fee_input.amount >= viewModel.minimumFeeInGroth && receiverAddrInput.acceptableInput }
                        onClicked: {
                            if (viewModel.isValidReceiverAddress(viewModel.receiverAddr)) {
                                confirmationDialog.addressText = viewModel.receiverAddr;
                                //% "BEAM"
                                confirmationDialog.amountText = amount_input.amount.toLocaleString(Qt.locale(), 'f', -128) + " " + qsTrId("general-beam");
                                //% "GROTH"
                                confirmationDialog.feeText = fee_input.amount.toLocaleString(Qt.locale(), 'f', -128) + " " + qsTrId("general-groth");

                                confirmationDialog.open();
                            } else {
                                //% "Address %1 is invalid"
                                var message = qsTrId("send-send-fail");
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
        }
    }

    Component {
        id: wallet_layout
        Item {            
            
            Row{
                anchors.top: parent.top
                anchors.right: parent.right
                spacing: 19

                CustomButton {
                    palette.button: Style.accent_incoming
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-receive-blue.svg"
                    //% "Receive"
                    text: qsTrId("wallet-receive-button")

                    onClicked: {
                        viewModel.generateNewAddress();
                        walletView.push(receive_layout);
                    }
                }

                CustomButton {
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Send"
                    text: qsTrId("general-send")

                    onClicked: {
                        walletView.push(send_layout);
                    }
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
                        onCopyValueText: viewModel.copyToClipboard(value)
                        onOpenExternal : function() {
                            var externalLink = "https://www.beam.mw/#exchanges";
                            if (viewModel.isAllowedBeamMWLinks) {
                                Qt.openUrlExternally(externalLink);
                            } else {
                                exchangesList.externalUrl = externalLink;
                                exchangesList.onOkClicked = function () {
                                    viewModel.isAllowedBeamMWLinks = true;
                                };
                                exchangesList.open();
                            }
                        }
                    }

                    SecondaryPanel {
                        Layout.minimumWidth: 350
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        //% "In progress"
                        title: qsTrId("wallet-in-progress-title")
                        receiving: viewModel.receiving
                        sending: viewModel.sending
                        maturing: viewModel.maturing

                        onCopyValueText: viewModel.copyToClipboard(value)
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
                        styleName: "Bold"; weight: Font.Bold
                    }

                    color: Style.content_main

                    //% "Transactions"
                    text: qsTrId("wallet-transactions-title")
                }

                CustomToolButton {
                    anchors.right: parent.right
                    icon.source: "qrc:/assets/icon-proof.svg"
                    //% "Verify payment"
                    ToolTip.text: qsTrId("wallet-verify-payment")
                    onClicked: {
                        paymentInfoVerifyDialog.model.reset();
                        paymentInfoVerifyDialog.open();
                    }
                }
            }

            CustomTableView {

                id: transactionsView

                anchors.fill: parent
                anchors.topMargin: 394-33
                Layout.bottomMargin: 9

                property int rowHeight: 69

                frameVisible: false
                selectionMode: SelectionMode.NoSelection
                backgroundVisible: false

                sortIndicatorVisible: true
                sortIndicatorColumn: 1
                sortIndicatorOrder: Qt.DescendingOrder

                Binding{
                    target: viewModel
                    property: "sortRole"
                    value: transactionsView.getColumn(transactionsView.sortIndicatorColumn).role
                }

                Binding{
                    target: viewModel
                    property: "sortOrder"
                    value: transactionsView.sortIndicatorOrder
                }

                property int resizableWidth: parent.width - iconColumn.width - actionsColumn.width

                TableViewColumn {
                    id: iconColumn
                    width: 60
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
                                anchors.left: parent.left
                                anchors.leftMargin: 26 
                                source: "qrc:/assets/beam-circle.svg"
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.dateRole
                    //% "Date | Time"
                    title: qsTrId("wallet-txs-date-time")
                    width: 160 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    resizable: false
                    movable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            SFLabel {
                                font.pixelSize: 14
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 20
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                                text: styleData.value
                                color: Style.content_main
                                copyMenuEnabled: true
                                onCopyText: viewModel.copyToClipboard(text)
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.userRole
                    //% "Address"
                    title: qsTrId("general-address")
                    width: 400 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideMiddle
                    resizable: false
                    movable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            SFLabel {
                                font.pixelSize: 14
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 20
                                elide: Text.ElideMiddle
                                anchors.verticalCenter: parent.verticalCenter
                                text: styleData.value
                                color: Style.content_main
                                copyMenuEnabled: true
                                onCopyText: viewModel.copyToClipboard(text)
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.amountRole
                    //% "Amount"
                    title: qsTrId("general-amount")
                    width: 200 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            property bool income: (styleData.row >= 0) ? viewModel.transactions[styleData.row].income : false
                            SFLabel {
                                anchors.leftMargin: 20
                                anchors.right: parent.right
                                anchors.left: parent.left
                                color: parent.income ? Style.accent_incoming : Style.accent_outgoing
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 24
                                text: (parent.income ? "+ " : "- ") + styleData.value
                                textFormat: Text.StyledText
                                font.styleName: "Light"
                                font.weight: Font.Thin
                                copyMenuEnabled: true
                                onCopyText: viewModel.copyToClipboard(styleData.value)
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.statusRole
                    //% "Status"
                    title: qsTrId("general-status")
                    width: 200 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                spacing: 14

                                SvgImage {
                                    Layout.alignment: Qt.AlignHCenter
                                    sourceSize: Qt.size(20, 20)
                                    source: getIconSource()

                                    function getIconSource() {
                                        if (!!viewModel.transactions[styleData.row]) {
                                            if (viewModel.transactions[styleData.row].isSelfTx()) {
                                                return "qrc:/assets/icon-transfer.svg";
                                            }

                                            return viewModel.transactions[styleData.row].income ? "qrc:/assets/icon-received.svg" : "qrc:/assets/icon-sent.svg";
                                        }
                                        return "qrc:/assets/icon-sent.svg";
                                    }
                                }

                                SFLabel {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    font.pixelSize: 14
                                    font.italic: true
                                    color: getTextColor()
                                    elide: Text.ElideRight
                                    text: txStatusText(styleData.value)
                                    copyMenuEnabled: true
                                    onCopyText: viewModel.copyToClipboard(text)

                                    function getTextColor () {
                                        if (!viewModel.transactions[styleData.row]) {
                                            return Style.content_main;
                                        }

                                        if (viewModel.transactions[styleData.row].inProgress() || viewModel.transactions[styleData.row].isCompleted()) {
                                            if (viewModel.transactions[styleData.row].isSelfTx()) {
                                                return Style.content_main;
                                            }
                                            return viewModel.transactions[styleData.row].income ? Style.accent_incoming : Style.accent_outgoing;
                                        }

                                        return Style.content_main;
                                    }

                                    function txStatusText(value) {
                                        switch(value) {
                                            //% "pending"
                                            case "pending": return qsTrId("wallet-txs-status-pending");
                                            //% "waiting for sender"
                                            case "waiting for sender": return qsTrId("wallet-txs-status-waiting-sender");
                                            //% "waiting for receiver"
                                            case "waiting for receiver": return qsTrId("wallet-txs-status-waiting-receiver");
                                            //% "receiving"
                                            case "receiving": return qsTrId("general-receiving");
                                            //% "sending"
                                            case "sending": return qsTrId("general-sending");
                                            //% "completed"
                                            case "completed": return qsTrId("wallet-txs-status-completed");
                                            //% "received"
                                            case "received": return qsTrId("wallet-txs-status-received");
                                            //% "sent"
                                            case "sent": return qsTrId("wallet-txs-status-sent");
                                            //% "cancelled"
                                            case "cancelled": return qsTrId("wallet-txs-status-cancelled");
                                            //% "expired"
                                            case "expired": return qsTrId("wallet-txs-status-expired");
                                            //% "failed"
                                            case "failed": return qsTrId("wallet-txs-status-failed");
                                            //% "unknown"
                                            default: return qsTrId("wallet-txs-status-unknown");
                                        }
                                    }
                                }
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
                                CustomToolButton {
                                    icon.source: "qrc:/assets/icon-actions.svg"
                                    //% "Actions"
                                    ToolTip.text: qsTrId("general-actions")
                                    onClicked: {
                                        txContextMenu.transaction = viewModel.transactions[styleData.row];
                                        txContextMenu.popup();
                                    }
                                }
                            }
                        }
                    }
                }

                ContextMenu {
                    id: txContextMenu
                    modal: true
                    dim: false
                    property TxObject transaction
                    Action {
                        //% "Copy address"
                        text: qsTrId("wallet-txs-copy-addr-cm")
                        icon.source: "qrc:/assets/icon-copy.svg"
                        onTriggered: {
                            if (!!txContextMenu.transaction)
                            {
                                viewModel.copyToClipboard(txContextMenu.transaction.user);
                            }
                        }
                    }
                    Action {
                        //% "Cancel"
                        text: qsTrId("general-cancel")
                        onTriggered: {
                           viewModel.cancelTx(txContextMenu.transaction);
                        }
                        enabled: !!txContextMenu.transaction && txContextMenu.transaction.canCancel
                        icon.source: "qrc:/assets/icon-cancel.svg"
                    }
                    Action {
                        //% "Delete"
                        text: qsTrId("general-delete")
                        icon.source: "qrc:/assets/icon-delete.svg"
                        enabled: !!txContextMenu.transaction && txContextMenu.transaction.canDelete
                        onTriggered: {
                            //% "The transaction will be deleted. This operation can not be undone"
                            deleteTransactionDialog.text = qsTrId("wallet-txs-delete-message");
                            deleteTransactionDialog.open();
                        }
                    }
                    Connections {
                        target: deleteTransactionDialog
                        onAccepted: {
                            viewModel.deleteTx(txContextMenu.transaction);
                        }
                    }
                }
                // Transaction details
                rowDelegate: Item {
                    height: transactionsView.rowHeight
                    id: rowItem
                    property bool collapsed: true

                    width: parent.width
                    Rectangle {
                            height: transactionsView.rowHeight
                            width: parent.width
                            color: Style.background_row_even
                            visible: styleData.alternate
                    }

                    Column {
                        id: rowColumn
                        width: parent.width
                        Rectangle {
                            height: transactionsView.rowHeight
                            width: parent.width
                            color: "transparent"
                        }
                        Item {
                            id: txDetails
                            height: 0
                            visible: height > 0
                            width: parent.width
                            clip: true

                            property int maximumHeight: detailsPanel.height

                            onMaximumHeightChanged: {
                                if (!rowItem.collapsed) {
                                    rowItem.height = maximumHeight + transactionsView.rowHeight
                                    txDetails.height = maximumHeight
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: Style.background_details
                            }
                            TransactionDetails {
                                id: detailsPanel
                                width: transactionsView.width
                                model: !!viewModel.transactions[styleData.row] ? viewModel.transactions[styleData.row] : null
                                onTextCopied: function (text) { viewModel.copyToClipboard(text);}
                                onShowDetails: {
                                    if (model)
                                    {
                                        paymentInfoDialog.model = model.getPaymentInfo();
                                        paymentInfoDialog.open();
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        height: transactionsView.rowHeight
                        width: parent.width

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
        }
    }

    StackView {
        id: walletView
        anchors.fill: parent
        initialItem: wallet_layout

        pushEnter: Transition {
            enabled: false
        }
        pushExit: Transition {
            enabled: false
        }
        popEnter: Transition {
            enabled: false
        }
        popExit: Transition {
            enabled: false
        }

        onCurrentItemChanged: {
            if (currentItem && currentItem.defaultFocusItem) {
                walletView.currentItem.defaultFocusItem.forceActiveFocus();
            }
        }
    }

    Component.onCompleted: {
        if (root.toSend) {
            walletView.push(send_layout);
            root.toSend = false;
        }
    }    
}
