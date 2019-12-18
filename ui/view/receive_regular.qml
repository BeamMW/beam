import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

ColumnLayout {
    id: receiveView

    property var defaultFocusItem: addressComment

    // callbacks set by parent
    property var onClosed: function() {}

    TopGradient {
        mainRoot: main
        topColor: Style.accent_incoming
    }

    ReceiveViewModel {
        id: viewModel
        onNewAddressFailed: {
            walletView.enabled = true
            var popup = Qt.createComponent("popup_message.qml")
                .createObject(receiveView)

            //% "You cannot generate new address. Your wallet doesn't have a master key."
            popup.message = qsTrId("can-not-generate-new-address-message")
            popup.open()
        }
    }

    function isValid() {
        return viewModel.commentValid
    }

    function saveAddress() {
        if (receiveView.isValid()) viewModel.saveAddress();
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
            //% "Receive"
            text:                qsTrId("wallet-receive-title")
        }
    }

    RowLayout {
        width:    parent.width
        spacing:  40

        ColumnLayout {
            id:                    receiveControls
            Layout.preferredWidth: parent.width * 0.65 - parent.spacing / 2
            Layout.alignment:      Qt.AlignTop

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
                Layout.fillWidth: true
                font.pixelSize:   14
                color:            Style.content_disabled
                readOnly:         true
                activeFocusOnTab: false
                text:             viewModel.receiverAddress
            }

            //
            // Amount
            //
            AmountInput {
                Layout.topMargin: 35
                //% "Receive amount (optional)"
                title:            qsTrId("receive-amount-label")
                id:               receiveAmountInput
                amount:           viewModel.amountToReceive
            }

            Binding {
                target:   viewModel
                property: "amountToReceive"
                value:    receiveAmountInput.amount
            }

            //
            // Comment
            //
            SFText {
                Layout.topMargin: 0//40
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
                    visible:        !viewModel.commentValid
                    font.italic:    true
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
                    Layout.topMargin: 26
                    font.pixelSize:   14
                    color:            Style.content_main
                    //% "Expires in"
                    text:             qsTrId("wallet-receive-expires-label")
                }

                CustomComboBox {
                    id:                  expiresCombo
                    Layout.topMargin:    26
                    Layout.minimumWidth: 75
                    height:              20
                    currentIndex:        viewModel.addressExpires

                    model: [
                        //% "24 hours"
                        qsTrId("wallet-receive-expires-24"),
                        //% "Never"
                        qsTrId("wallet-receive-expires-never")
                    ]
                }

                Binding {
                    target:   viewModel
                    property: "addressExpires"
                    value:    expiresCombo.currentIndex
                }
            }
        }

        //
        //  QR Code
        //
        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.35 - parent.spacing / 2
            Layout.topMargin:      10
            Layout.alignment:      Qt.AlignTop

            Image {
                Layout.alignment: Qt.AlignHCenter
                fillMode: Image.Pad
                source: viewModel.receiverAddressQR
            }

            SFText {
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 14
                font.italic: true
                color: Style.content_main
                //% "Scan to send"
                text: qsTrId("wallet-receive-qr-label")
            }
        }
    }

    SFText {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 30
        font.pixelSize:   14
        color:            Style.content_main
        //% "Send this address to the sender over an external secure channel or scan the QR code"
        text: qsTrId("wallet-receive-addr-message")
    }

    SFText {
        Layout.alignment:      Qt.AlignHCenter
        Layout.preferredWidth: 470
        Layout.maximumHeight:  40
        Layout.topMargin:      30
        font.pixelSize:        14
        color:                 Style.content_disabled
        wrapMode:              Text.WordWrap
        horizontalAlignment:   Text.AlignHCenter
        leftPadding:           15
        rightPadding:          15
        //% "For the transaction to complete, you should get online during the 12 hours after Beams are sent."
        text: qsTrId("wallet-receive-text-online-time")
    }

    Row {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 30
        spacing:          25

        CustomButton {
            //% "Close"
            text:               qsTrId("general-close")
            palette.buttonText: Style.content_main
            icon.source:        "qrc:/assets/icon-cancel-white.svg"
            onClicked:          {
                receiveView.saveAddress();
                onClosed();
            }
        }

        CustomButton {
            //% "Copy transaction address"
            text:               qsTrId("wallet-receive-copy-address")
            palette.buttonText: Style.content_opposite
            icon.color:         Style.content_opposite
            palette.button:     Style.active
            icon.source:        "qrc:/assets/icon-copy.svg"
            onClicked:          {
                BeamGlobals.copyToClipboard(viewModel.receiverAddress);
                receiveView.saveAddress();
                onClosed();
            }
            enabled:            receiveView.isValid()
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
