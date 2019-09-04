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
    property var  defaultFocusItem: receiveAmountInput.amountInput
    property bool addressSaved: false

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
        return receiveAmountInput.isValid && sentAmountInput.isValid && !currencyError()
    }

    function canSend () {
        if (!isValid()) return false;
        if (viewModel.amountToReceive <= 0 || viewModel.amountSent <= 0) return false;
        return true;
    }

    function currencyError() {
        if (viewModel.receiveCurrency == viewModel.sentCurrency) return true;
        if (viewModel.receiveCurrency != Currency.CurrBeam && viewModel.sentCurrency != Currency.CurrBeam) return true;
        return false;
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
            //
            // Amount
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
            // Sent amount
            //
            AmountInput {
                Layout.topMargin: 35
                //% "Sent amount"
                title:            qsTrId("sent-amount-label")
                id:               sentAmountInput
                color:            Style.accent_outgoing
                hasFee:           true
                currency:         viewModel.sentCurrency
                amount:           viewModel.amountSent
                multi:            true
                currColor:        currencyError() ? Style.validator_error : Style.content_main

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
                //% "Rate"
                text:             qsTrId("general-rate")
            }

            SFText {
                id:               rate
                Layout.topMargin: 3
                font.pixelSize:   14
                color:            Style.content_secondary
                text:             ["1", receiveAmountInput.currencyLabel, "=", Utils.calcDisplayRate(receiveAmountInput, sentAmountInput), sentAmountInput.currencyLabel].join(" ")
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
                thisView.parent.parent.pop();
            }
        }

        CustomButton {
            text:                qsTrId("wallet-receive-copy-address")
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
            }
        }
    }

    Row {
        Layout.fillHeight: true
    }
}
