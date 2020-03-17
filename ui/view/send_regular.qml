import QtQuick 2.11
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.4
import Beam.Wallet 1.0
import "controls"
import "./utils.js" as Utils

ColumnLayout {
    id: sendRegularView

    property var defaultFocusItem: receiverTAInput

    // callbacks set by parent
    property var onAccepted: undefined
    property var onClosed: undefined
    property var onSwapToken: undefined

    TopGradient {
        mainRoot: main
        topColor: Style.accent_outgoing
    }

    SendViewModel {
        id: viewModel

        onSendMoneyVerified: {
            onAccepted();
        }

        onCantSendToExpired: {
            Qt.createComponent("send_expired.qml")
                .createObject(sendRegularView)
                .open();
        }
    }

    function isTAInputValid() {
        return viewModel.receiverTA.length == 0 || viewModel.receiverTAValid
    }

    Row {
        Layout.alignment:    Qt.AlignHCenter
        Layout.topMargin:    75
        Layout.bottomMargin: 40

        SFText {
            font.pixelSize:  18
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            //% "Send"
            text:            qsTrId("send-title")
        }
    }

    RowLayout  {
        Layout.fillWidth: true
        spacing:    70

        //
        // Left column
        //
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.leftMargin: 25
            Layout.fillWidth: true
            Layout.preferredWidth: 100

            SFText {
                font.pixelSize:  14
                font.styleName:  "Bold"; font.weight: Font.Bold
                color:           Style.content_main
                //% "Transaction token or contact"
                text:            qsTrId("send-send-to-label")
                bottomPadding:   26
            }

            SFTextInput {
                Layout.fillWidth: true
                id:               receiverTAInput
                font.pixelSize:   14
                color:            isTAInputValid() ? Style.content_main : Style.validator_error
                backgroundColor:  isTAInputValid() ? Style.content_main : Style.validator_error
                font.italic :     !isTAInputValid()
                text:             viewModel.receiverTA
                validator:        RegExpValidator { regExp: /[0-9a-zA-Z]{1,}/ }
                selectByMouse:    true
                //% "Please specify contact or transaction token"
                placeholderText:  qsTrId("send-contact-placeholder")

                onTextChanged: {
                    if (BeamGlobals.isSwapToken(text)&&
                        typeof onSwapToken == "function") {
                        onSwapToken(text);
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                SFText {
                    Layout.alignment: Qt.AlignTop
                    id:               receiverTAError
                    color:            Style.validator_error
                    font.pixelSize:   12
                    //% "Invalid wallet address"
                    text:             qsTrId("wallet-send-invalid-address-or-token")
                    visible:          !isTAInputValid()
                }
            }

            Binding {
                target:   viewModel
                property: "receiverTA"
                value:    receiverTAInput.text
            }

            SFText {
                Layout.topMargin: 25
                font.pixelSize:   14
                font.styleName:   "Bold"; font.weight: Font.Bold
                color:            Style.content_main
                //% "Comment"
                text:             qsTrId("general-comment")
                topPadding:    5
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

        //
        // Right column
        //
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.rightMargin: 35
            Layout.fillWidth: true
            Layout.preferredWidth: 100

            AmountInput {
                //% "Send"
                title:            qsTrId("send-title")
                id:               sendAmountInput
                amountIn:         viewModel.sendAmount
                secondCurrencyRateValue:    viewModel.secondCurrencyRateValue
                secondCurrencyLabel:        viewModel.secondCurrencyLabel
                setMaxAvailableAmount:      function() { viewModel.setMaxAvailableAmount(); }
                hasFee:           true
                showAddAll:       true
                color:            Style.accent_outgoing
                //% "Insufficient funds: you would need %1 to complete the transaction"
                error:            viewModel.isEnough ? "" : qsTrId("send-founds-fail").arg(Utils.uiStringToLocale(viewModel.missing))
            }

            Binding {
                target:   viewModel
                property: "sendAmount"
                value:    sendAmountInput.amount
            }

            Binding {
                target:   viewModel
                property: "feeGrothes"
                value:    sendAmountInput.fee
            }

            GridLayout {
                Layout.topMargin:    50
                Layout.alignment:    Qt.AlignTop
                Layout.minimumWidth: 400
                columnSpacing:       20
                rowSpacing:          10
                columns:             2

                Rectangle {
                    x:      0
                    y:      0
                    width:  parent.width
                    height: parent.height
                    radius: 10
                    color:  Style.background_second
                }

                SFText {
                    Layout.alignment:       Qt.AlignTop
                    Layout.topMargin:       30
                    Layout.leftMargin:      25
                    font.pixelSize:         14
                    color:                  Style.content_secondary
                    //% "Total UTXO value"
                    text:                   qsTrId("send-total-label") + ":"
                }

                BeamAmount
                {
                    Layout.alignment:       Qt.AlignTop
                    Layout.topMargin:       30
                    Layout.rightMargin:     25
                    error:                  !viewModel.isEnough
                    amount:                 viewModel.totalUTXO
                    currencySymbol:         BeamGlobals.getCurrencyLabel(Currency.CurrBeam)
                    secondCurrencyLabel:    viewModel.secondCurrencyLabel
                    secondCurrencyRateValue: viewModel.secondCurrencyRateValue
                }

                SFText {
                    Layout.alignment:       Qt.AlignTop
                    Layout.leftMargin:      25
                    font.pixelSize:         14
                    color:                  Style.content_secondary
                    //% "Amount to send"
                    text:                   qsTrId("send-amount-label") + ":"
                }

                BeamAmount
                {
                    Layout.alignment:       Qt.AlignTop
                    Layout.rightMargin:     25
                    error:                  !viewModel.isEnough
                    amount:                 viewModel.sendAmount
                    currencySymbol:         BeamGlobals.getCurrencyLabel(Currency.CurrBeam)
                    secondCurrencyLabel:    viewModel.secondCurrencyLabel
                    secondCurrencyRateValue: viewModel.secondCurrencyRateValue
                }

                SFText {
                    Layout.alignment:       Qt.AlignTop
                    Layout.leftMargin:      25
                    font.pixelSize:         14
                    color:                  Style.content_secondary
                    text:                   qsTrId("general-change") + ":"
                }

                BeamAmount
                {
                    Layout.alignment:       Qt.AlignTop
                    Layout.rightMargin:     25
                    error:                  !viewModel.isEnough
                    amount:                 viewModel.change
                    currencySymbol:         BeamGlobals.getCurrencyLabel(Currency.CurrBeam)
                    secondCurrencyLabel:    viewModel.secondCurrencyLabel
                    secondCurrencyRateValue: viewModel.secondCurrencyRateValue
                }

                SFText {
                    Layout.alignment:       Qt.AlignTop
                    Layout.leftMargin:      25
                    Layout.bottomMargin:    30
                    font.pixelSize:         14
                    color:                  Style.content_secondary
                    //% "Remaining"
                    text:                   qsTrId("send-remaining-label") + ":"
                }

                BeamAmount
                {
                    Layout.alignment:       Qt.AlignTop
                    Layout.rightMargin:     25
                    Layout.bottomMargin:    30
                    error:                  !viewModel.isEnough
                    amount:                 viewModel.available
                    currencySymbol:         BeamGlobals.getCurrencyLabel(Currency.CurrBeam)
                    secondCurrencyLabel:    viewModel.secondCurrencyLabel
                    secondCurrencyRateValue: viewModel.secondCurrencyRateValue
                }
            }
        }
    }

    Row {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 40
        spacing:          25

        CustomButton {
            //% "Back"
            text:        qsTrId("general-back")
            icon.source: "qrc:/assets/icon-back.svg"
            onClicked:   onClosed();
        }

        CustomButton {
            //% "Send"
            text:               qsTrId("general-send")
            palette.buttonText: Style.content_opposite
            palette.button:     Style.accent_outgoing
            icon.source:        "qrc:/assets/icon-send-blue.svg"
            enabled:            viewModel.canSend
            onClicked: {                
                const dialogComponent = Qt.createComponent("send_confirm.qml");
                const dialogObject = dialogComponent.createObject(sendRegularView,
                    {
                        addressText: viewModel.receiverAddress,
                        currency: Currency.CurrBeam,
                        amount: viewModel.sendAmount,
                        fee: viewModel.feeGrothes,
                        onAcceptedCallback: acceptedCallback,
                        secondCurrencyRate: viewModel.secondCurrencyRateValue,
                        secondCurrencyLabel: viewModel.secondCurrencyLabel
                    }).open();

                function acceptedCallback() {
                    viewModel.sendMoney();
                }
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
