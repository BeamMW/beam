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
    id: sendSwapView
    
    property var defaultFocusItem: comment_input
    property var predefinedTxParams: undefined

    // callbacks set by parent
    property var onAccepted: undefined
    property var onClosed: undefined

    TopGradient {
        mainRoot: main
        topColor: Style.accent_outgoing
    }

    function validateCoin() {
        var currency = viewModel.sendCurrency
        if (currency == Currency.CurrBeam) {
            currency = viewModel.receiveCurrency;

            if (currency == Currency.CurrBeam) return;
        }

        if (!BeamGlobals.canReceive(currency)) {
/*% "%1 is not connected, 
please review your settings and try again"
*/
            swapna.text = qsTrId("swap-currency-na-message").arg(BeamGlobals.getCurrencyName(currency))
            swapna.open()
            return false;
        }

        return true;
    }

    function setToken(token) {
        viewModel.token = token
        validateCoin();
    }

    SwapNADialog {
        id:         swapna
        onRejected: sendSwapView.onClosed();
        onAccepted: main.openSwapSettings();
    }

    Component.onCompleted: {
        comment_input.forceActiveFocus();
        if (predefinedTxParams != undefined) {
            viewModel.setParameters(predefinedTxParams);
            validateCoin();
        }
    }

    SendSwapViewModel {
        id: viewModel

        // TODO:SWAP Implement the necessary callbacks and error handling for the send operation
        /*
        onSendMoneyVerified: {
           parent.enabled = true
           walletView.pop()
        }

        onCantSendToExpired: {
            parent.enabled = true;
            Qt.createComponent("send_expired.qml")
                .createObject(sendView)
                .open();
        }
        */
    }

    Timer {
        interval: 1000
        repeat:   true
        running:  true

        onTriggered: {
            const expired = viewModel.expiresTime < (new Date())
            expiresTitle.color = expired ? Style.validator_error : Style.content_main
            expires.color = expired ? Style.validator_error : Style.content_secondary
        }
    }

    Row {
        Layout.alignment:    Qt.AlignHCenter
        Layout.topMargin:    75
        Layout.bottomMargin: 40

        SFText {
            font.pixelSize:  18
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            //% "Accept Swap Offer"
            text:            qsTrId("wallet-send-swap-title")
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

            Grid  {
                Layout.fillWidth: true
                columnSpacing:    70
                columns:          2

                ColumnLayout {
                    width: parent.width / 2 - parent.columnSpacing / 2

                    AmountInput {
                        Layout.topMargin: 25
                        //% "Send amount"
                        title:            qsTrId("sent-amount-label")
                        id:               sendAmountInput
                        hasFee:           true
                        currFeeTitle:     true
                        amountIn:         viewModel.sendAmount
                        currency:         viewModel.sendCurrency
                        secondCurrencyRateValue:    viewModel.secondCurrencySendRateValue
                        secondCurrencyLabel:        viewModel.secondCurrencyLabel
                        readOnlyA:        true
                        multi:            false
                        color:            Style.accent_outgoing
                        currColor:        viewModel.receiveCurrency == viewModel.sendCurrency || getErrorText().length ? Style.validator_error : Style.content_main
                        error:            getErrorText()

                        function getErrorText () {
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
                    }

                    Binding {
                        target:   viewModel
                        property: "sendFee"
                        value:    sendAmountInput.fee
                    }

                    //
                    // Comment
                    //
                    SFText {
                        Layout.topMargin: 25
                        font.pixelSize:   14
                        font.styleName:   "Bold"; font.weight: Font.Bold
                        color:            Style.content_main
                        //% "Comment"
                        text:             qsTrId("general-comment")
                    }

                    SFTextInput {
                        id:               comment_input
                        Layout.fillWidth: true
                        font.pixelSize:   14
                        color:            Style.content_main
                        selectByMouse:    true
                        maximumLength:    BeamGlobals.maxCommentLength()
                    }

                    Item {
                        Layout.fillWidth: true
                        SFText {
                            Layout.alignment: Qt.AlignTop
                            color:            Style.content_secondary
                            font.italic:      true
                            font.pixelSize:   12
                            //% "Comments are local and won't be shared"
                            text:             qsTrId("general-comment-local")
                        }
                    }

                    Binding {
                        target:   viewModel
                        property: "comment"
                        value:    comment_input.text
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    width: parent.width / 2 - parent.columnSpacing / 2

                    //
                    // Receive Amount
                    //
                    AmountInput {
                        Layout.topMargin: 25
                        //% "Receive amount"
                        title:            qsTrId("receive-amount-swap-label")
                        id:               receiveAmountInput
                        hasFee:           true
                        currFeeTitle:     true
                        amountIn:         viewModel.receiveAmount
                        currency:         viewModel.receiveCurrency
                        secondCurrencyRateValue:    viewModel.secondCurrencyReceiveRateValue
                        secondCurrencyLabel:        viewModel.secondCurrencyLabel
                        readOnlyA:        true
                        multi:            false
                        color:            Style.accent_incoming
                        currColor:        viewModel.receiveCurrency == viewModel.sendCurrency || getErrorText().length ? Style.validator_error : Style.content_main
                        error:            getErrorText()
                        showTotalFee:     true

                        function getErrorText() {
                            if(!viewModel.isReceiveFeeOK) {
                                //% "The swap amount must be greater than the transaction fee"
                                return qsTrId("send-less-than-fee")
                            }
                            return ""
                        }
                    }

                    Binding {
                        target:   viewModel
                        property: "receiveFee"
                        value:    receiveAmountInput.fee
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 25
                        columnSpacing:    30
                        columns:          2

                        ColumnLayout {
                            SFText {
                                font.pixelSize:   14
                                font.styleName:   "Bold"; font.weight: Font.Bold
                                color:            Style.content_main
                                //% "Offered on"
                                text:             qsTrId("wallet-send-swap-offered-label")
                            }

                            SFText {
                                Layout.topMargin: 10
                                id:               offered
                                font.pixelSize:   14
                                color:            Style.content_secondary
                                text:             Utils.formatDateTime(viewModel.offeredTime, BeamGlobals.getLocaleName())
                            }
                        }

                        ColumnLayout {
                            SFText {
                                id:               expiresTitle
                                font.pixelSize:   14
                                font.styleName:   "Bold"; font.weight: Font.Bold
                                color:            Style.content_main
                                //% "Expires on"
                                text:             qsTrId("wallet-send-swap-expires-label")
                            }

                            SFText {
                                id:               expires
                                Layout.topMargin: 10
                                font.pixelSize:   14
                                color:            Style.content_secondary
                                text:             Utils.formatDateTime(viewModel.expiresTime, BeamGlobals.getLocaleName())
                            }
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

                    SFText {
                        id:               rate
                        Layout.topMargin: 3
                        font.pixelSize:   14
                        color:            Style.content_secondary
                        text:             viewModel.isSendBeam
                            ? ["1", sendAmountInput.currencyLabel, "=", Utils.uiStringToLocale(viewModel.rate), receiveAmountInput.currencyLabel].join(" ")
                            : ["1", receiveAmountInput.currencyLabel, "=", Utils.uiStringToLocale(viewModel.rate), sendAmountInput.currencyLabel].join(" ")
                    }
                }
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
                        text:            qsTrId("accept-swap-token")
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
                        text:                viewModel.token
                        copyMenuEnabled:     true
                        onCopyText:          BeamGlobals.copyToClipboard(text)
                        wrapMode:            Text.WrapAnywhere
                        color:               Style.content_secondary
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                spacing:          25

                CustomButton {
                    text:                qsTrId("general-close")
                    palette.buttonText:  Style.content_main
                    icon.source:         "qrc:/assets/icon-cancel-white.svg"
                    onClicked:           sendSwapView.onClosed();
                }

                CustomButton {
                    //% "Swap"
                    text:               qsTrId("general-swap")
                    palette.buttonText: Style.content_opposite
                    palette.button:     Style.accent_outgoing
                    icon.source:        "qrc:/assets/icon-send-blue.svg"
                    enabled:            viewModel.canSend
                    onClicked: {
                        if (!validateCoin()) return;

                        const dialogComponent = Qt.createComponent("send_confirm.qml");
                        var dialogObject = dialogComponent.createObject(sendSwapView,
                            {
                                swapMode: true,
                                addressText: viewModel.receiverAddress,
                                amountText: [Utils.uiStringToLocale(viewModel.sendAmount), sendAmountInput.getCurrencyLabel()].join(" "),
                                feeText: [Utils.uiStringToLocale(viewModel.sendFee), sendAmountInput.getFeeLabel()].join(" "),
                                feeLabel: sendAmountInput.currency == Currency.CurrBeam ?
                                    //% "BEAM Transaction fee"
                                    qsTrId("beam-transaction-fee") + ":" :
                                    //% "%1 Transaction fee rate"
                                    qsTrId("general-fee-rate").arg(sendAmountInput.getCurrencyLabel()),
                                // TODO: move swapCurrencyLabel logic to control
                                swapCurrencyLabel: sendAmountInput.currency == Currency.CurrBeam ? "" : sendAmountInput.getCurrencyLabel(),
                                onAcceptedCallback: acceptedCallback,
                                secondCurrencyRate: viewModel.secondCurrencySendRateValue,
                                secondCurrencyLabel: viewModel.secondCurrencyLabel
                            }).open();

                        function acceptedCallback() {
                            viewModel.sendMoney();
                            sendSwapView.onAccepted();
                        }
                    }
                }
            }
        }
    }
}
