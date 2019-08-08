import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

Item {
    ReceiveViewModel {
        id: viewModel

        onNewAddressFailed: {
            walletView.enabled = true;
            Qt.createComponent("receive_addrfail.qml")
                .createObject(sendView)
                .open();
        }
    }

    id: receiveView
    property Item defaultFocusItem: addressComment
    property bool isValidComment: true

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
                        text: viewModel.receiverAddress
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
                            currentIndex: viewModel.expires ? 1 : 0

                            Binding {
                                target: viewModel
                                property: "addressExpires"
                                value: !expiresControl.currentIndex
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
                                property: "amountToReceive"
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
                        id: addressComment
                        font.pixelSize: 14
                        width: parent.width
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

                        source: viewModel.receiverAddressQR
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
                    BeamGlobals.copyToClipboard(myAddressID.text);
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
