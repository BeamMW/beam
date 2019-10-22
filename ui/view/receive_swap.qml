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

    // callbacks set by parent
    property var    modeSwitchEnabled: true
    property var    onClosed: undefined
    property var    onRegularMode: undefined

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
        if (!viewModel.commentValid) return false
        if (viewModel.receiveCurrency == viewModel.sentCurrency) return false
        return receiveAmountInput.isValid && sentAmountInput.isValid && !currencyError() && rateRow.rateValid()
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

    Component.onCompleted: {
        if (!BeamGlobals.canSwap()) swapna.open();
    }

    SwapNADialog {
        id: swapna
        onRejected: thisView.onClosed()
        onAccepted: main.openSwapSettings()
        //% "You do not have any 3rd-party currencies connected.\nUpdate your settings and try again."
        text:       qsTrId("swap-na-message").replace("\\n", "\n")
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
            //% "Create swap offer"
            text:                qsTrId("wallet-receive-swap-title")
        }

        CustomSwitch {
            id:         mode
            //% "Swap"
            text:       qsTrId("general-swap")
            x:          parent.width - width
            checked:    true
            enabled:    modeSwitchEnabled
            visible:    modeSwitchEnabled
            onClicked: {
                if (!checked) onRegularMode();
            }
        }
    }

    ColumnLayout {
        //
        // My Address
        //
        SFText {
            font.pixelSize: 14
            font.styleName: "Bold"; font.weight: Font.Bold
            color: Style.content_main
            //% "My address (auto-generated)"
            text: qsTrId("wallet-receive-my-addr-label")
        }

        SFTextInput {
            id:               myAddressID
            font.pixelSize:   14
            color:            Style.content_disabled
            readOnly:         true
            activeFocusOnTab: false
            text:             viewModel.receiverAddress
        }
    }

    Grid {
        Layout.fillWidth: true
        columnSpacing:    70
        columns:          2

        ColumnLayout {
            width: parent.width / 2 - parent.columnSpacing / 2

            AmountInput {
                Layout.topMargin: 35
                //% "Send amount"
                title:            qsTrId("sent-amount-label")
                id:               sentAmountInput
                color:            Style.accent_outgoing
                hasFee:           true
                currency:         viewModel.sentCurrency
                amount:           viewModel.amountSent
                multi:            true
                resetAmount:      false
                currColor:        currencyError() ? Style.validator_error : Style.content_main
                //% "There is not enough funds to complete the transaction"
                error:            viewModel.isGreatThanFee ? (viewModel.isEnough ? "" : qsTrId("send-not-enough")) 
                                    //% "The swap amount must be greater than the redemption fee."
                                    : qsTrId("send-less-than-fee")

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
        }

        ColumnLayout {
            width: parent.width / 2 - parent.columnSpacing / 2

            //
            // Receive Amount
            //
            AmountInput {
                Layout.topMargin: 35
                //% "Receive amount"
                title:            qsTrId("receive-amount-swap-label")
                id:               receiveAmountInput
                hasFee:           true
                currency:         viewModel.receiveCurrency
                amount:           viewModel.amountToReceive
                multi:            true
                resetAmount:      false
                currColor:        currencyError() ? Style.validator_error : Style.content_main

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
                    color:            Style.content_main
                    //% "Offer expiration time"
                    text:             qsTrId("wallet-receive-offer-expires-label")
                }

                CustomComboBox {
                    id:                  expiresCombo
                    Layout.topMargin:    18
                    Layout.minimumWidth: 75
                    height:              20
                    currentIndex:        viewModel.offerExpires

                    model: [
                        //% "12 hours"
                        qsTrId("wallet-receive-expires-12"),
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

            SFText {
                Layout.topMargin: 18
                font.pixelSize:   14
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

                function calcReceiveAmount () {
                    var rate = parseFloat(rateInput.text) || 0
                    if (rate == 0) return {amount:0, error: false}

                    var ramount = sentAmountInput.amount * rate
                    var error = ramount > maxAmount || ramount < minAmount

                    if (ramount > maxAmount) ramount = maxAmount
                    if (ramount < minAmount) ramount = minAmount

                    return {
                        amount:    ramount.toFixed(8).replace(/\.?0+$/,""),
                        error:     error,
                        //% "Invalid rate"
                        errorText: qsTrId("swap-invalid-rate")
                    }
                }

                function calcRate () {
                    return Utils.calcDisplayRate(receiveAmountInput, sentAmountInput, rateInput.focus)
                }

                function rateValid () {
                   return !calcReceiveAmount().error && !calcRate().error
                }

                function rateError () {
                    return calcRate().errorText || calcReceiveAmount().errorText || ""
                }

                function recalcAmount () {
                     if (sentAmountInput.amount == 0) sentAmountInput.amount = 1
                     receiveAmountInput.amount = calcReceiveAmount().amount
                }

                SFText {
                    font.pixelSize:   14
                    color:            rateRow.rateValid() ? Style.content_secondary : Style.validator_error
                    text:             ["1", sentAmountInput.currencyLabel, "="].join(" ")
                }

                SFTextInput {
                    id:                  rateInput
                    padding:             0
                    Layout.minimumWidth: 22
                    activeFocusOnTab:    true
                    font.pixelSize:      14
                    color:               rateRow.rateValid() ? Style.content_main : Style.validator_error
                    backgroundColor:     rateRow.rateValid() ? Style.content_main : Style.validator_error
                    text:                rateRow.calcRate().displayRate
                    selectByMouse:       true
                    maximumLength:       30
                    validator:           RegExpValidator {regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,27}[1-9])?$/}

                    onTextEdited: {
                        // unbind
                        text = text
                        // update
                        rateRow.recalcAmount()
                    }

                    onFocusChanged: {
                        if (focus) {
                            var rcalc = rateRow.calcRate()
                            if (rcalc.rate < rcalc.minRate) {
                                text = rcalc.minDisplayRate
                                rateRow.recalcAmount()
                            }
                        }
                        if (!focus) {
                            text = Qt.binding(function() {
                                    return rateRow.calcRate().displayRate
                                })
                        }
                    }
                }

                SFText {
                    font.pixelSize:  14
                    color:           rateRow.rateValid() ? Style.content_secondary : Style.validator_error
                    text:            receiveAmountInput.currencyLabel
                }
            }

            Item {
                Layout.leftMargin: rateInput.x
                SFText {
                    color:               Style.validator_error
                    font.pixelSize:      12
                    font.styleName:      "Italic"
                    width:               parent.width
                    text:                rateRow.rateError()
                    visible:             !rateRow.rateValid()
                }
            }
        }
    }

    SFText {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 40
        font.pixelSize:   14
        font.styleName:   "Bold"
        font.weight:      Font.Bold
        color:            Style.content_main
        //% "Your transaction token:"
        text: qsTrId("wallet-receive-your-token")
    }

    SFTextArea {
        Layout.alignment:    Qt.AlignHCenter
        width:               570
        focus:               true
        activeFocusOnTab:    true
        font.pixelSize:      14
        wrapMode:            TextInput.Wrap
        color:               isValid() ? (canSend() ? Style.content_secondary : Qt.darker(Style.content_secondary)) : Style.validator_error
        text:                viewModel.transactionToken
        horizontalAlignment: TextEdit.AlignHCenter
        readOnly:            true
        enabled:             false
    }

    SFText {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 5
        font.pixelSize:   14
        color:            Style.content_main
        //% "Send this token to the sender over an external secure channel"
        text: qsTrId("wallet-swap-token-message")
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
            //% "copy transaction token"
            text:                qsTrId("wallet-receive-copy-token")
            palette.buttonText:  Style.content_opposite
            icon.color:          Style.content_opposite
            palette.button:      Style.passive
            icon.source:         "qrc:/assets/icon-copy.svg"
            enabled:             thisView.canSend()
            onClicked: {
                BeamGlobals.copyToClipboard(viewModel.transactionToken);
                if (!thisView.addressSaved) {
                    thisView.addressSaved = true
                    viewModel.saveAddress()
                }
                viewModel.startListen()
                onClosed()
            }
        }

        CustomButton {
            //% "publish transaction token"
            text:                qsTrId("wallet-receive-swap-publish")
            palette.buttonText:  Style.content_opposite
            icon.color:          Style.content_opposite
            palette.button:      Style.active
            icon.source:         "qrc:/assets/icon-share.svg"
            enabled:             thisView.canSend()
            onClicked: {
                if (!thisView.addressSaved) {
                    thisView.addressSaved = true
                    viewModel.saveAddress()
                }
                viewModel.startListen()
                viewModel.publishToken()
                onClosed()
            }
        }
    }

    Row {
        Layout.fillHeight: true
    }
}
