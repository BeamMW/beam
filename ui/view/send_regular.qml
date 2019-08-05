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
    property variant parentView: null

    function setToken(token) {
        viewModel.receiverTA = token
        sendAmountInput.amountInput.forceActiveFocus();
    }

    SendViewModel {
        id: viewModel

        onSendMoneyVerified: {
           thisView.enabled = true
           walletView.pop()
        }

        onCantSendToExpired: {
            thisView.enabled = true;
            Qt.createComponent("send_expired.qml")
                .createObject(sendView)
                .open();
        }
    }

    function isTAInputValid() {
        return viewModel.receiverTA.length == 0 || viewModel.receiverTAValid
    }

    GridLayout  {
        Layout.fillWidth: true
        columnSpacing:    70
        columns:          2

        ColumnLayout {
            SFText {
                font.pixelSize:  14
                font.styleName:  "Bold"; font.weight: Font.Bold
                color:           Style.content_main
                text:            qsTrId("send-send-to-label") //% "Transaction token or contact"
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
                placeholderText:  qsTrId("send-contact-placeholder") //% "Please specify contact"

                onTextChanged: {
                    if (BeamGlobals.isSwapToken(text)) {
                        parentView.onSwapToken(text)
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
                    text:             qsTrId("wallet-send-invalid-token") //% "Invalid address or token"
                    visible:          !isTAInputValid()
                }
            }

            Binding {
                target:   viewModel
                property: "receiverTA"
                value:    receiverTAInput.text
            }

            //
            // Amount
            //
            AmountInput {
                Layout.topMargin: 25
                title:            qsTrId("send-amount-label") //% "Transaction amount"
                id:               sendAmountInput
                amount:           viewModel.sendAmount
                hasFee:           true
                color:            Style.accent_outgoing
                error:            viewModel.isEnough ? "" : qsTrId("send-founds-fail").arg(Utils.formatAmount(viewModel.missing))
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

            SFText {
                Layout.topMargin: 25
                font.pixelSize:   14
                font.styleName:   "Bold"; font.weight: Font.Bold
                color:            Style.content_main
                text:             qsTrId("general-comment") //% "Comment"
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
        GridLayout {
            Layout.alignment:    Qt.AlignTop
            Layout.minimumWidth: 370
            columnSpacing:       20
            columns:             2

            Rectangle {
                x: 0
                y: 0
                width: parent.width
                height: parent.height
                radius:       10
                color:        Style.background_second
            }

            SFText {
                Layout.topMargin:  20
                Layout.leftMargin: 25
                font.pixelSize:    14
                color:             Style.content_secondary
                text:              qsTrId("send-total-label") + ":"
            }

            BeamAmount
            {
                Layout.topMargin:   20
                Layout.rightMargin: 25
                error:              !viewModel.isEnough
                amount:             viewModel.totalUTXO
            }

            SFText {
                Layout.topMargin:  15
                Layout.leftMargin: 25
                font.pixelSize:    14
                color:             Style.content_secondary
                text:              qsTrId("send-amount-label") + ":"
            }

            BeamAmount
            {
                Layout.topMargin:   15
                Layout.rightMargin: 25
                error:              !viewModel.isEnough
                amount:             viewModel.sendAmount
            }

            SFText {
                Layout.topMargin:  15
                Layout.leftMargin: 25
                font.pixelSize:    14
                color:             Style.content_secondary
                text:              qsTrId("general-change") + ":"
            }

            BeamAmount
            {
                Layout.topMargin:   15
                Layout.rightMargin: 25
                error:              !viewModel.isEnough
                amount:             viewModel.change
            }

            SFText {
                Layout.topMargin:    15
                Layout.leftMargin:   25
                Layout.bottomMargin: 20
                font.pixelSize:      14
                color:               Style.content_secondary
                text:                qsTrId("send-remaining-label") + ":"
            }

            BeamAmount
            {
                Layout.topMargin:    15
                Layout.rightMargin:  25
                Layout.bottomMargin: 20
                error:               viewModel.available < 0
                amount:              viewModel.available
            }
        }
    }

    Row {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 40
        spacing:          25

        CustomButton {
            text:        qsTrId("general-back") //% "Back"
            icon.source: "qrc:/assets/icon-back.svg"
            onClicked:   walletView.pop();
        }

        CustomButton {
            text:               qsTrId("general-send") //% "Send"
            palette.buttonText: Style.content_opposite
            palette.button:     Style.accent_outgoing
            icon.source:        "qrc:/assets/icon-send-blue.svg"
            enabled:            viewModel.canSend
            onClicked: {
                const dialog       = Qt.createComponent("send_confirm.qml").createObject(thisView);
                dialog.addressText = viewModel.receiverAddress;
                dialog.amountText  = [Utils.formatAmount(viewModel.sendAmount), qsTrId("general-beam")].join(" ")
                dialog.feeText     = [Utils.formatAmount(viewModel.feeGrothes), qsTrId("general-groth")].join(" ")
                dialog.open();
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
