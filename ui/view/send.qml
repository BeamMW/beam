import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

Item {
    SendViewModel {
        id: viewModel

        onSendMoneyVerified: {
           walletView.enabled = true;
           walletView.pop();
        }

        onCantSendToExpired: {
            walletView.enabled = true;
            Qt.createComponent("send_expired.qml")
                .createObject(sendView)
                .open();
        }
    }

    id: sendView
    property Item defaultFocusItem: receiverAddrInput

    ConfirmationDialog {
        id: invalidAddressDialog
        //% "Got it"
        okButtonText: qsTrId("invalid-addr-got-it-button")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 73
        anchors.bottomMargin: 30

        SFText {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 18
            font.styleName: "Bold"; font.weight: Font.Bold
            color: Style.content_main
            //% "Send Beam"
            text: qsTrId("send-title")
        }

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: 10
            Layout.maximumHeight: 30
        }

        RowLayout {
            Layout.fillWidth: true
            //Layout.topMargin: 50

            spacing: 70

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                height: childrenRect.height

                ColumnLayout {
                    width: parent.width

                    spacing: 12

                    SFText {
                        font.pixelSize: 14
                        font.styleName: "Bold"; font.weight: Font.Bold
                        color: Style.content_main
                        //% "Send To"
                        text: qsTrId("send-send-to-label") + ":"
                    }

                    SFTextInput {
                        Layout.fillWidth: true

                        id: receiverAddrInput
                        font.pixelSize: 14
                        color: Style.content_main
                        text: viewModel.receiverAddress

                        validator: RegExpValidator { regExp: /[0-9a-fA-F]{1,80}/ }
                        selectByMouse: true

                        //% "Please specify contact"
                        placeholderText: qsTrId("send-contact-placeholder")

                        onTextChanged : {
                            receiverAddressError.visible = receiverAddrInput.text.length > 0 && !viewModel.isValidReceiverAddress(receiverAddrInput.text)
                        }
                    }

                    SFText {
                        Layout.alignment: Qt.AlignTop
                        id: receiverAddressError
                        color: Style.validator_error
                        font.pixelSize: 10
                        //% "Invalid address"
                        text: qsTrId("general-invalid-address")
                        visible: false
                    }

                    SFText {
                        id: receiverName
                        color: Style.content_main
                        font.pixelSize: 14
                        font.styleName: "Bold"; font.weight: Font.Bold
                    }

                    Binding {
                        target: viewModel
                        property: "receiverAddress"
                        value: receiverAddrInput.text
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                height: childrenRect.height

                ColumnLayout {
                    width: parent.width

                    spacing: 12

                    SFText {
                        font.pixelSize: 14
                        font.styleName: "Bold"; font.weight: Font.Bold
                        color: Style.content_main
                        //% "Transaction amount"
                        text: qsTrId("send-amount-label")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        SFTextInput {
                            Layout.fillWidth: true

                            id: amount_input

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
                            property: "sendAmount"
                            value: amount_input.amount
                        }

                        SFText {
                            font.pixelSize: 24
                            color: Style.content_main
                            //% "BEAM"
                            text: qsTrId("general-beam")
                        }
                    }
                    Item {
                        Layout.topMargin: -12
                        Layout.minimumHeight: 16
                        Layout.fillWidth: true

                        SFText {
                            //% "Insufficient funds: you would need %1 to complete the transaction"
                            text: qsTrId("send-founds-fail").arg(viewModel.missing)
                            color: Style.validator_error
                            font.pixelSize: 14
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 10
                            font.styleName: "Italic"
                            width: parent.width
                            visible: !viewModel.isEnough
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            spacing: 70

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                height: childrenRect.height

                ColumnLayout {
                    width: parent.width

                    spacing: 12

                    SFText {
                        font.pixelSize: 14
                        font.styleName: "Bold"; font.weight: Font.Bold
                        color: Style.content_main
                        //% "Comment"
                        text: qsTrId("general-comment")
                    }

                    SFTextInput {
                        id: comment_input
                        Layout.fillWidth: true

                        font.pixelSize: 14
                        color: Style.content_main

                        maximumLength: 1024
                        selectByMouse: true
                    }

                    Binding {
                        target: viewModel
                        property: "comment"
                        value: comment_input.text
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                height: childrenRect.height

                ColumnLayout {
                    width: parent.width

                    spacing: 12

                    SFText {
                        font.pixelSize: 14
                        font.styleName: "Bold"; font.weight: Font.Bold
                        color: Style.content_main
                        //% "Transaction fee"
                        text: qsTrId("general-fee")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true

                            SFTextInput {
                                Layout.fillWidth: true
                                id: fee_input

                                font.pixelSize: 36
                                font.styleName: "Light"; font.weight: Font.Light
                                color: Style.accent_outgoing

                                text: viewModel.defaultFeeInGroth.toLocaleString(Qt.locale(), 'f', -128)

                                property int amount: viewModel.defaultFeeInGroth

                                validator: IntValidator {bottom: viewModel.minimumFeeInGroth}
                                maximumLength: 15
                                selectByMouse: true

                                onTextChanged: {
                                    if (focus) {
                                        amount = text ? text : 0;
                                    }
                                }

                                onFocusChanged: {
                                    if (amount >= 0) {
                                        // QLocale::FloatingPointShortest = -128
                                        text = focus ? amount : amount.toLocaleString(Qt.locale(), 'f', -128);
                                    }
                                }
                            }
                        }

                        SFText {
                            font.pixelSize: 24
                            color: Style.content_main
                            //% "GROTH"
                            text: qsTrId("send-curency-sub-name")
                        }
                    }

                    Item {
                        Layout.topMargin: -12
                        Layout.minimumHeight: 16
                        Layout.fillWidth: true

                        SFText {
                            //% "The minimum fee is %1 GROTH"
                            text: qsTrId("send-fee-fail").arg(viewModel.minimumFeeInGroth)
                            color: Style.validator_error
                            font.pixelSize: 14
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 10
                            font.styleName: "Italic"
                            width: parent.width
                            visible: fee_input.amount < viewModel.minimumFeeInGroth
                        }
                    }

                    Binding {
                        target: viewModel
                        property: "feeGrothes"
                        //value: feeSlider.value
                        value: fee_input.amount
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 20
                        height: 96

                        Item {
                            anchors.fill: parent

                            RowLayout {
                                anchors.fill: parent
                                readonly property int margin: 15
                                anchors.leftMargin: margin
                                anchors.rightMargin: margin
                                spacing: margin

                                Item {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignCenter
                                    height: childrenRect.height

                                    ColumnLayout {
                                        width: parent.width
                                        spacing: 10

                                        SFText {
                                            Layout.alignment: Qt.AlignHCenter
                                            font.pixelSize: 18
                                            font.styleName: "Bold"; font.weight: Font.Bold
                                            color: Style.content_secondary
                                            //% "Remaining"
                                            text: qsTrId("send-remaining-label")
                                        }

                                        RowLayout
                                        {
                                            Layout.alignment: Qt.AlignHCenter
                                            spacing: 6
                                            clip: true

                                            SFText {
                                                font.pixelSize: 24
                                                font.styleName: "Light"; font.weight: Font.Light
                                                color: Style.content_secondary
                                                text: viewModel.available
                                            }

                                            SvgImage {
                                                Layout.topMargin: 4
                                                sourceSize: Qt.size(16, 24)
                                                source: "qrc:/assets/b-grey.svg"
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    id: separator
                                    Layout.fillHeight: true
                                    Layout.topMargin: 10
                                    Layout.bottomMargin: 10
                                    width: 1
                                    color: Style.content_secondary
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignCenter
                                    height: childrenRect.height

                                    ColumnLayout {
                                        width: parent.width
                                        spacing: 10

                                        SFText {
                                            Layout.alignment: Qt.AlignHCenter
                                            font.pixelSize: 18
                                            font.styleName: "Bold"; font.weight: Font.Bold
                                            color: Style.content_secondary
                                            //% "Change"
                                            //: UTXO type Change 
                                            text: qsTrId("general-change")
                                        }

                                        RowLayout
                                        {
                                            Layout.alignment: Qt.AlignHCenter
                                            spacing: 6
                                            clip: true

                                            SFText {
                                                font.pixelSize: 24
                                                font.styleName: "Light"; font.weight: Font.Light
                                                color: Style.content_secondary
                                                text: viewModel.change
                                            }

                                            SvgImage {
                                                Layout.topMargin: 4
                                                sourceSize: Qt.size(16, 24)
                                                source: "qrc:/assets/b-grey.svg"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: Style.white
                            opacity: 0.1
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: 10
            Layout.maximumHeight: 30
        }

        Row {
            Layout.alignment: Qt.AlignHCenter

            spacing: 30

            CustomButton {
                //% "Back"
                text: qsTrId("general-back")
                icon.source: "qrc:/assets/icon-back.svg"
                onClicked: {
                    walletView.pop();
                }
            }

            CustomButton {
                //% "Send"
                text: qsTrId("general-send")
                palette.buttonText: Style.content_opposite
                palette.button: Style.accent_outgoing
                icon.source: "qrc:/assets/icon-send-blue.svg"
                enabled: {viewModel.isEnough && amount_input.amount > 0 && fee_input.amount >= viewModel.minimumFeeInGroth && receiverAddrInput.acceptableInput }
                onClicked: {
                    if (viewModel.isValidReceiverAddress(viewModel.receiverAddress)) {
                        const component = Qt.createComponent("send_confirm.qml");
                        const confirmDialog = component.createObject(sendView);

                        confirmDialog.addressText = viewModel.receiverAddress;
                        //% "BEAM"
                        confirmDialog.amountText = amount_input.amount.toLocaleString(Qt.locale(), 'f', -128) + " " + qsTrId("general-beam");
                        //% "GROTH"
                        confirmDialog.feeText = fee_input.amount.toLocaleString(Qt.locale(), 'f', -128) + " " + qsTrId("general-groth");

                        confirmDialog.open();
                    } else {
                        //% "Address %1 is invalid"
                        var message = qsTrId("send-send-fail");
                        invalidAddressDialog.text = message.arg(viewModel.receiverAddress);
                        invalidAddressDialog.open();
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}