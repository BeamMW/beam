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

        var isOtherCurrActive  = false
        var currname = ""

        if (currency == Currency.CurrBtc) {
            isOtherCurrActive  = BeamGlobals.haveBtc()
            //% "Bitcoin"
            currname = qsTrId("general-bitcoin")
        }

        if (currency == Currency.CurrLtc){
            isOtherCurrActive = BeamGlobals.haveLtc()
            //% "Litecoin"
            currname = qsTrId("general-litecoin")
        }

        if (currency == Currency.CurrQtum) {
            isOtherCurrActive = BeamGlobals.haveQtum()
            //% "QTUM"
            currname = qsTrId("general-qtum")
        }

        if (isOtherCurrActive == false) {
            //% "%1 is not connected, \nplease review your settings and try again."
            swapna.text = qsTrId("swap-currency-na-message").arg(currname).replace("\\n", "\n")
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
            //% "Swap currencies"
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

            ColumnLayout {
                Layout.fillWidth: true
                visible: predefinedTxParams == undefined

                SFText {
                    font.pixelSize:  14
                    font.styleName:  "Bold"; font.weight: Font.Bold
                    color:           Style.content_main
                    //% "Transaction token"
                    text:            qsTrId("send-swap-to-label")
                }

                SFTextInput {
                    Layout.fillWidth: true
                    id:               tokenInput
                    font.pixelSize:   14
                    color:            viewModel.tokenValid ? Style.content_main : Style.validator_error
                    backgroundColor:  viewModel.tokenValid ? Style.content_main : Style.validator_error
                    font.italic :     !viewModel.tokenValid
                    text:             viewModel.token
                    validator:        RegExpValidator { regExp: /[0-9a-fA-F]{1,}/ }
                    selectByMouse:    true
                    readOnly:         true
                    //% "Please specify contact or transaction token"
                    placeholderText:  qsTrId("send-contact-placeholder")
                }

                Item {
                    Layout.fillWidth: true
                    SFText {
                        Layout.alignment: Qt.AlignTop
                        id:               receiverTAError
                        color:            Style.validator_error
                        font.pixelSize:   12
                        //% "Invalid address"
                        text:             qsTrId("general-invalid-address")
                        visible:          !viewModel.tokenValid
                    }
                }

                Binding {
                    target:   viewModel
                    property: "token"
                    value:    tokenInput.text
                }
            }

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
                        amount:           viewModel.sendAmount
                        currency:         viewModel.sendCurrency
                        readOnlyA:        true
                        multi:            false
                        color:            Style.accent_outgoing
                        currColor:        viewModel.receiveCurrency == viewModel.sendCurrency ? Style.validator_error : Style.content_main
                        //% "There is not enough funds to complete the transaction"
                        error:            viewModel.isEnough ? "" : qsTrId("send-not-enough")
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
                        amount:           viewModel.receiveAmount
                        currency:         viewModel.receiveCurrency
                        readOnlyA:        true
                        multi:            false
                        color:            Style.accent_incoming
                        currColor:        viewModel.receiveCurrency == viewModel.sendCurrency ? Style.validator_error : Style.content_main
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
                        text:             ["1", sendAmountInput.currencyLabel, "=", Utils.calcDisplayRate(receiveAmountInput, sendAmountInput).displayRate, receiveAmountInput.currencyLabel].join(" ")
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 60
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
                                addressText: viewModel.receiverAddress,
                                amountText: [Utils.uiStringToLocale(viewModel.sendAmount), sendAmountInput.getCurrencyLabel()].join(" "),
                                feeText: [Utils.uiStringToLocale(viewModel.sendFee), sendAmountInput.getFeeLabel()].join(" "),
                                onAcceptedCallback: acceptedCallback
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
