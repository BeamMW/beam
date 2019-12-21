import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"
import "./utils.js" as Utils

ColumnLayout {
    id: thisView

    property var  defaultFocusItem: sentAmountInput.amountInput
    property bool addressSaved: false
    property var locale: Qt.locale()

    // callbacks set by parent
    property var onClosed: undefined

    TopGradient {
        mainRoot: main
        topColor: Style.accent_incoming
    }

    ReceiveSwapViewModel {
        id: viewModel
        onNewAddressFailed: {
            thisView.enabled = true
            Qt.createComponent("receive_addrfail.qml")
                .createObject(sendView)
                .open();
        }
    }

    function isValid () {
        if (!viewModel.commentValid) return false;
        if (viewModel.receiveCurrency == viewModel.sentCurrency) return false;
        return receiveAmountInput.isValid && sentAmountInput.isValid && !currencyError() && rateRow.rateValid;
    }

    function canSend () {
        if (!isValid()) return false;
        if (parseFloat(viewModel.amountToReceive) <= 0 || parseFloat(viewModel.amountSent) <= 0) return false;
        return true;
    }

    function currencyError() {
        if (viewModel.receiveCurrency == viewModel.sentCurrency) return true;
        if (viewModel.receiveCurrency != Currency.CurrBeam && viewModel.sentCurrency != Currency.CurrBeam) return true;
        return false;
    }

    function saveAddress() {
        if (!thisView.addressSaved) {
            thisView.addressSaved = true
            viewModel.saveAddress()
        }
    }

    Component.onCompleted: {
        if (!BeamGlobals.canSwap()) swapna.open();
    }

    SwapNADialog {
        id: swapna
        onRejected: thisView.onClosed()
        onAccepted: main.openSwapSettings()
/*% "You do not have any 3rd-party currencies connected.
Update your settings and try again."
*/
        text:       qsTrId("swap-na-message")
    }

    Item {
        Layout.fillWidth:    true
        Layout.topMargin:    75
        Layout.bottomMargin: 50

        SFText {
            x:                   parent.width / 2 - width / 2
            font.pixelSize:      18
            font.styleName:      "Bold"; font.weight: Font.Bold
            color:               Style.content_main
            //% "Create a Swap Offer"
            text:                qsTrId("wallet-receive-swap-title")
        }
    }

    ScrollView {
        id:                  scrollView
        Layout.fillWidth:    true
        Layout.fillHeight:   true
        Layout.bottomMargin: 10
        clip:                true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy:   ScrollBar.AsNeeded

        ColumnLayout {
            width: scrollView.availableWidth

            Grid {
                Layout.fillWidth: true
                columnSpacing:    10
                columns:          3

                ColumnLayout {
                    width: parent.width / 2 - parent.columnSpacing / 2 - 20

                    AmountInput {
                        Layout.topMargin: 35
                        //% "Send amount"
                        title:            qsTrId("sent-amount-label")
                        id:               sentAmountInput
                        color:            Style.accent_outgoing
                        hasFee:           true
                        currFeeTitle:     true
                        currency:         viewModel.sentCurrency
                        amount:           viewModel.amountSent
                        multi:            true
                        resetAmount:      false
                        currColor:        currencyError() || !BeamGlobals.canReceive(currency) ? Style.validator_error : Style.content_main
                        error:            getErrorText()

                        function getErrorText() {
                            if(!BeamGlobals.canReceive(currency)) {
/*% "%1 is not connected, 
please review your settings and try again"
*/
                                return qsTrId("swap-currency-na-message").arg(BeamGlobals.getCurrencyName(currency)).replace("\n", "")
                            }
                            if(!viewModel.isSendFeeOK) {
                                //% "The swap amount must be greater than the transaction fee"
                                return qsTrId("send-less-than-fee")
                            }
                            if(!viewModel.isEnough) {
                                //% "There is not enough funds to complete the transaction"
                                return qsTrId("send-not-enough")
                            }
                            return ""
                        }

                        onCurrencyChanged: {
                            if(sentAmountInput.currency != Currency.CurrBeam) {
                                if(receiveAmountInput.currency != Currency.CurrBeam) {
                                    receiveAmountInput.currency = Currency.CurrBeam
                                }
                            }
                        }
                    }

                    Binding {
                        target:   viewModel
                        property: "amountSent"
                        value:    sentAmountInput.amount
                    }

                    Binding {
                        target:   viewModel
                        property: "sentCurrency"
                        value:    sentAmountInput.currency
                    }

                    Binding {
                        target:   viewModel
                        property: "sentFee"
                        value:    sentAmountInput.fee
                    }

                    Connections {
                        target: viewModel
                        onSentFeeChanged: sentAmountInput.fee = viewModel.sentFee
                    }

                    //
                    // Comment
                    //
                    SFText {
                        Layout.topMargin: 40
                        font.pixelSize:   14
                        font.styleName:   "Bold"; font.weight: Font.Bold
                        color:            Style.content_main
                        //% "Comment"
                        text:             qsTrId("general-comment")
                    }

                    SFTextInput {
                        id:               addressComment
                        font.pixelSize:   14
                        Layout.fillWidth: true
                        font.italic :     !viewModel.commentValid
                        backgroundColor:  viewModel.commentValid ? Style.content_main : Style.validator_error
                        color:            viewModel.commentValid ? Style.content_main : Style.validator_error
                        focus:            true
                        text:             viewModel.addressComment
                        maximumLength:    BeamGlobals.maxCommentLength()
                        enabled:          !thisView.addressSaved
                    }

                    Binding {
                        target:   viewModel
                        property: "addressComment"
                        value:    addressComment.text
                    }

                    Item {
                        Layout.fillWidth: true
                        SFText {
                            //% "Address with the same comment already exists"
                            text:           qsTrId("general-addr-comment-error")
                            color:          Style.validator_error
                            font.pixelSize: 12
                            font.italic:    true
                            visible:        !viewModel.commentValid
                        }
                    }
                }  // ColumnLayout

                ColumnLayout {
                    Item {
                        height: 75
                    }
                    SvgImage {
                        Layout.maximumHeight: 26
                        Layout.maximumWidth: 26
                        source: "qrc:/assets/icon-swap-currencies.svg"
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var sentCurency = sentAmountInput.currency;
                                sentAmountInput.currency = receiveAmountInput.currency;
                                receiveAmountInput.currency = sentCurency;
                            }
                        }
                    }
                }

                ColumnLayout {
                    width: parent.width / 2 - parent.columnSpacing / 2 - 20

                    //
                    // Receive Amount
                    //
                    AmountInput {
                        Layout.topMargin: 35
                        //% "Receive amount"
                        title:            qsTrId("receive-amount-swap-label")
                        id:               receiveAmountInput
                        hasFee:           true
                        currFeeTitle:     true
                        currency:         viewModel.receiveCurrency
                        amount:           viewModel.amountToReceive
                        multi:            true
                        resetAmount:      false
                        currColor:        currencyError() || !BeamGlobals.canReceive(currency) ? Style.validator_error : Style.content_main
                        error:            getErrorText()
                        showTotalFee:     true

                        function getErrorText() {
                            if(!BeamGlobals.canReceive(currency)) {
/*% "%1 is not connected, 
please review your settings and try again"
*/
                                return qsTrId("swap-currency-na-message").arg(BeamGlobals.getCurrencyName(currency)).replace("\n", "")
                            }
                            if(!viewModel.isReceiveFeeOK) {
                                //% "The swap amount must be greater than the transaction fee"
                                return qsTrId("send-less-than-fee")
                            }
                            return ""
                        }

                        onCurrencyChanged: {
                            if(receiveAmountInput.currency != Currency.CurrBeam) {
                                if(sentAmountInput.currency != Currency.CurrBeam) {
                                    sentAmountInput.currency = Currency.CurrBeam
                                }
                            }
                        }
                    }

                    Binding {
                        target:   viewModel
                        property: "amountToReceive"
                        value:    receiveAmountInput.amount
                    }

                    Binding {
                        target:   viewModel
                        property: "receiveCurrency"
                        value:    receiveAmountInput.currency
                    }

                    Binding {
                        target:   viewModel
                        property: "receiveFee"
                        value:    receiveAmountInput.fee
                    }

                    Connections {
                        target: viewModel
                        onReceiveFeeChanged: receiveAmountInput.fee = viewModel.receiveFee
                    }

                    SFText {
                        Layout.topMargin: 18
                        font.pixelSize:   14
                        font.styleName:   "Bold"
                        font.weight:      Font.Bold
                        color:            Style.content_main
                        //% "Exchange rate"
                        text:             qsTrId("general-rate")
                    }

                    RowLayout {
                        id: rateRow
                        Layout.topMargin: 3
                        Layout.fillWidth: true

                        property double maxAmount: 254000000
                        property double minAmount: 0.00000001
                        property bool rateValid:   true
                        property bool lockedByReceiveAmount: false

                        function changeRate() {
                            if (!rateInput.focus && !lockedByReceiveAmount) {
                                rateInput.rate = viewModel.rate;
                                rateInput.text = rateInput.rate == "0" ? "" : Utils.uiStringToLocale(rateInput.rate);
                            }
                        }

                        function changeReceive(byRate) {
                            lockedByReceiveAmount = true;
                            var rateValue =
                                parseFloat(Utils.localeDecimalToCString(rateInput.rate)) || 0;
                            if (sentAmountInput.amount != "0" && rateValue) {
                                receiveAmountInput.amount= viewModel.isSendBeam
                                    ? BeamGlobals.multiplyWithPrecision8(sentAmountInput.amount, rateValue)
                                    : BeamGlobals.divideWithPrecision8(sentAmountInput.amount, rateValue);
                            } else if (byRate && !rateValue) {
                                receiveAmountInput.amount = "0";
                            } else if (!byRate && sentAmountInput.amount == "0") {
                                lockedByReceiveAmount = false;
                                receiveAmountInput.amount = "0";
                            }
                            lockedByReceiveAmount = false;
                        }

                        function checkIsRateValid() {
                            var rate = parseFloat(Utils.localeDecimalToCString(rateInput.rate)) || 0;
                            if (rate == 0 ||
                                receiveAmountInput.amount == "0") {
                                rateValid = true;
                                return;
                            }
                            rateValid =
                                parseFloat(receiveAmountInput.amount) <= rateRow.maxAmount &&
                                parseFloat(receiveAmountInput.amount) >= rateRow.minAmount;
                        }

                        SFText {
                            font.pixelSize:   14
                            color:            rateRow.rateValid ? Style.content_secondary : Style.validator_error
                            text:             viewModel.isSendBeam
                                ? ["1", sentAmountInput.currencyLabel, "="].join(" ")
                                : ["1", receiveAmountInput.currencyLabel, "="].join(" ") 
                        }

                        SFTextInput {
                            property string rate: "0"

                            id:                  rateInput
                            padding:             0
                            Layout.minimumWidth: 35
                            activeFocusOnTab:    true
                            font.pixelSize:      14
                            color:               rateRow.rateValid ? Style.content_main : Style.validator_error
                            backgroundColor:     rateRow.rateValid ? Style.content_main : Style.validator_error
                            text:                ""
                            selectByMouse:       true
                            maximumLength:       30
                            
                            validator: DoubleValidator {
                                bottom: rateRow.minAmount
                                top: rateRow.maxAmount
                                decimals: 8
                                locale: locale.name
                                notation: DoubleValidator.StandardNotation
                            }

                            onFocusChanged: {
                                text = rate == "0" ? "" : (rateInput.focus ? rate : Utils.uiStringToLocale(Utils.localeDecimalToCString(rate)));
                                if (focus) cursorPosition = positionAt(rateInput.getMousePos().x, rateInput.getMousePos().y);
                            }

                            onTextEdited: {
                                if (rateInput.focus) {
                                    if (text.match("^00*$")) {
                                        text = "0";
                                    }

                                    var value = text ? text.split(locale.groupSeparator).join('') : "0";
                                    var parts = value.split(locale.decimalPoint);
                                    var left = (parseInt(parts[0], 10) || 0).toString();
                                    rate = parts[1] ? [left, parts[1]].join(locale.decimalPoint) : left;
                                    if (!parseFloat(Utils.localeDecimalToCString(rate))) {
                                        rate = "0";
                                    }
                                    rateRow.changeReceive(true);
                                    rateRow.checkIsRateValid();
                                }
                            }

                            Component.onCompleted: {
                                viewModel.amountSentChanged.connect(rateRow.changeReceive);
                                viewModel.rateChanged.connect(rateRow.changeRate);
                            }
                        }

                        SFText {
                            font.pixelSize:  14
                            color:           rateRow.rateValid ? Style.content_secondary : Style.validator_error
                            text:            viewModel.isSendBeam ? receiveAmountInput.currencyLabel : sentAmountInput.currencyLabel
                        }
                    }

                    Item {
                        Layout.leftMargin: rateInput.x
                        SFText {
                            color:               Style.validator_error
                            font.pixelSize:      12
                            font.styleName:      "Italic"
                            width:               parent.width
                            //% "Invalid rate"
                            text:                qsTrId("swap-invalid-rate")
                            visible:             !rateRow.rateValid
                        }
                    }

                    //
                    // Expires
                    //
                    RowLayout {
                        id:      expiresCtrl
                        spacing: 10
                        property alias title: expiresTitle.text

                        SFText {
                            id:               expiresTitle
                            Layout.topMargin: 18
                            font.pixelSize:   14
                            font.styleName:   "Bold"
                            font.weight:      Font.Bold
                            color:            Style.content_main
                            //% "Offer expiration time"
                            text:             qsTrId("wallet-receive-offer-expires-label")
                        }

                        CustomComboBox {
                            id:                  expiresCombo
                            Layout.topMargin:    18
                            Layout.minimumWidth: 90
                            height:              20
                            currentIndex:        viewModel.offerExpires

                            model: [
                                //% "15 minutes"
                                qsTrId("wallet-receive-expires-15m"),
                                //% "30 minutes"
                                qsTrId("wallet-receive-expires-30m"),
                                //% "1 hour"
                                qsTrId("wallet-receive-expires-1"),
                                //% "2 hours"
                                qsTrId("wallet-receive-expires-2"),
                                //% "6 hours"
                                qsTrId("wallet-receive-expires-6")
                            ]
                        }

                        Binding {
                            target:   viewModel
                            property: "offerExpires"
                            value:    expiresCombo.currentIndex
                        }
                    }
                }  // ColumnLayout
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 30
                Layout.alignment: Qt.AlignHCenter
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    SFText {
                        font.pixelSize:  14
                        font.styleName:  "Bold"; font.weight: Font.Bold
                        color:           Style.content_main
                        //% "Your swap token"
                        text:            qsTrId("wallet-receive-swap-your-token")
                    }

                    Item {
                        width:  17
                        height: 1
                    }

                    SvgImage {
                        source:  tokenRow.visible ? "qrc:/assets/icon-grey-arrow-down.svg" : "qrc:/assets/icon-grey-arrow-up.svg"
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                tokenRow.visible = !tokenRow.visible;
                            }
                        }
                    }
                }

                Row {
                    id:      tokenRow
                    visible: false
                    Layout.topMargin: 10
                    SFLabel {
                        horizontalAlignment: Text.AlignHCenter
                        width:               392
                        font.pixelSize:      14
                        color:               isValid() ? (canSend() ? Style.content_secondary : Qt.darker(Style.content_secondary)) : Style.validator_error
                        text:                viewModel.transactionToken
                        wrapMode:            Text.WrapAnywhere
                    }
                }

                Row {
                    visible: tokenRow.visible
                    Layout.topMargin: 10
                    Layout.alignment: Qt.AlignHCenter
                    SFText {
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize:   14
                        color:            Style.content_main
                        //% "Send this token to the sender over a secure external channel"
                        text: qsTrId("wallet-swap-token-message")
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                spacing:          25

                CustomButton {
                    //% "Close"
                    text: qsTrId("general-close")
                    palette.buttonText: Style.content_main
                    icon.source: "qrc:/assets/icon-cancel-white.svg"
                    onClicked: {
                        onClosed();
                    }
                }

                CustomButton {
                    //% "copy swap token"
                    text:                qsTrId("wallet-receive-copy-token")
                    palette.buttonText:  Style.content_opposite
                    icon.color:          Style.content_opposite
                    palette.button:      Style.passive
                    icon.source:         "qrc:/assets/icon-copy.svg"
                    enabled:             thisView.canSend()
                    onClicked: {
                        BeamGlobals.copyToClipboard(viewModel.transactionToken);
                        thisView.saveAddress();
                        viewModel.startListen();
                        onClosed();
                    }
                }

                CustomButton {
                    //% "publish offer"
                    text:                qsTrId("wallet-receive-swap-publish")
                    palette.buttonText:  Style.content_opposite
                    icon.color:          Style.content_opposite
                    palette.button:      Style.active
                    icon.source:         "qrc:/assets/icon-share.svg"
                    enabled:             thisView.canSend()
                    onClicked: {
                        thisView.saveAddress();
                        viewModel.startListen();
                        viewModel.publishToken();
                        onClosed();
                    }
                }
            }
        }
    }
}
