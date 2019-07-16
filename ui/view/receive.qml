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
    property Item defaultFocusItem: addressComment
    property bool isValidComment: true

    ReceiveViewModel {
        id: viewModel
        onNewAddressFailed: {
            walletView.enabled = true;
            Qt.createComponent("receive_addrfail.qml")
                .createObject(sendView)
                .open();
        }
    }

    SFText {
        Layout.alignment:     Qt.AlignHCenter
        Layout.topMargin:     75
        Layout.bottomMargin:  30
        font.pixelSize:       18
        font.styleName:       "Bold"; font.weight: Font.Bold
        color:                Style.content_main
        text:                 qsTrId("wallet-receive-title") //% "Receive"
    }

    RowLayout {
        width: parent.width
        spacing:40

        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.6
            Layout.fillWidth: false

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
                id: myAddressID
                width: parent.width
                font.pixelSize: 14
                color: Style.content_disabled
                readOnly: true
                activeFocusOnTab: false
                text: viewModel.receiverAddress
            }

            //
            // Amount
            //
            SFText {
                Layout.topMargin: 18
                font.pixelSize:   14
                font.styleName:   "Bold"; font.weight: Font.Bold
                color:            Style.content_main
                text:             qsTrId("receive-amount-label") //% "Receive amount (optional)"
            }

            RowLayout {
                Layout.fillWidth: true

                SFTextInput {
                    Layout.fillWidth: true
                    id: receiveAmountInput
                    font.pixelSize: 36
                    font.styleName: "Light"; font.weight: Font.Light
                    color: Style.accent_incoming

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
                    property: "amountToReceive"
                    value: receiveAmountInput.amount
                }

                SFText {
                    Layout.topMargin:   8
                    font.pixelSize:     24
                    font.letterSpacing: 0.6
                    color:              Style.content_main
                    text:               qsTrId("general-beam") //% "BEAM"
                }
            }

            //
            // Comment
            //
            SFText {
                Layout.topMargin: 18
                font.pixelSize:   14
                font.styleName:   "Bold"; font.weight: Font.Bold
                color:            Style.content_main
                text:             qsTrId("general-comment") //% "Comment"
            }

            SFTextInput {
                id: addressComment
                font.pixelSize: 14
                Layout.fillWidth: true
                font.italic : !isValidComment
                backgroundColor: isValidComment ? Style.content_main: Style.validator_error
                color: isValidComment ? Style.content_main : Style.validator_error
                focus: true
                text: viewModel.addressComment
                onTextEdited: {
                    isValidComment = viewModel.isValidComment(addressComment.text);
                    if (isValidComment) {
                        viewModel.addressComment = addressComment.text;
                    }
                }
            }

            SFText {
                //% "Address with same comment already exist"
                text: qsTrId("general-addr-comment-error")
                color: Style.validator_error
                font.pixelSize: 12
                visible: !isValidComment
            }

            //
            // Expires
            //
            RowLayout {
                spacing: 10

                SFText {
                    Layout.topMargin: 18
                    font.pixelSize:   14
                    font.italic:      true
                    color:            Style.content_main
                    text:             qsTrId("wallet-receive-expires-label") + ":" //% "Expires"
                }

                CustomComboBox {
                    id:                  expiresControl
                    Layout.topMargin:    18
                    Layout.minimumWidth: 75
                    height:              20
                    currentIndex:        viewModel.expires ? 1 : 0

                    Binding {
                        target:   viewModel
                        property: "addressExpires"
                        value:    !expiresControl.currentIndex
                    }

                    model: [
                        //% "24 hours"
                        qsTrId("wallet-receive-expires-24"),
                        //% "Never"
                        qsTrId("wallet-receive-expires-never")
                    ]
                }
            }
        }

        //
        //  QR Code
        //
        ColumnLayout {
            Layout.preferredWidth: parent.width * 0.4
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
        Layout.topMargin: 40
        font.pixelSize:   14
        color:            Style.content_main
        //% "Send this address to the sender over an external secure channel"
        text: qsTrId("wallet-receive-propogate-addr-message")
    }

    Row {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 40
        spacing:          25

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
            //% "Copy transaction address"
            text: qsTrId("wallet-receive-copy-address")
            palette.buttonText: Style.content_opposite
            icon.color: Style.content_opposite
            palette.button: Style.active
            icon.source: "qrc:/assets/icon-copy.svg"
            onClicked: {
                BeamGlobals.copyToClipboard(myAddressID.text);
            }
        }
    }

    Row {
        Layout.fillHeight: true
    }
}
