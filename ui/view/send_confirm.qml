import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.1
import Beam.Wallet 1.0
import "controls"
import "./utils.js" as Utils

ConfirmationDialog {
    onVisibleChanged: {
        if (!this.visible) {
            this.destroy();
        }
    }

    id: sendViewConfirm
    parent: Overlay.overlay

    property var    onAcceptedCallback: undefined
    property alias  addressText:        addressLabel.text
    property int    currency:           Currency.CurrBeam
    property string amount:             "0"
    property string fee:                "0"
    property string secondCurrencyRate:         "0"
    property string secondCurrencyLabel:        ""
    property bool   swapMode:           false
    readonly property string    currencyLabel:  BeamGlobals.getCurrencyLabel(sendViewConfirm.currency)
    readonly property string    feeLabel:       !sendViewConfirm.swapMode
                                                //% "Transaction fee"
                                                ? (qsTrId("general-fee") + ":")
                                                : (sendViewConfirm.currency == Currency.CurrBeam
                                                    //% "BEAM Transaction fee"
                                                    ? (qsTrId("beam-transaction-fee") + ":")
                                                    //% "%1 Transaction fee rate"
                                                    : qsTrId("general-fee-rate").arg(currencyLabel))
    property Item   defaultFocusItem:   BeamGlobals.needPasswordToSpend() ? requirePasswordInput : cancelButton

    okButtonText: sendViewConfirm.swapMode ?
                    //% "Swap"
                    qsTrId("general-swap"):
                    //% "Send"
                    qsTrId("general-send")
    okButtonColor:           Style.accent_outgoing
    okButtonIconSource:      "qrc:/assets/icon-send-blue.svg"
    okButtonEnable:          BeamGlobals.needPasswordToSpend() ? requirePasswordInput.text.length : true
    cancelButtonIconSource:  "qrc:/assets/icon-cancel-white.svg"

    function confirmationHandler() {
        if (BeamGlobals.needPasswordToSpend()) {
            if (requirePasswordInput.text.length == 0) {
                requirePasswordInput.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
            if (!BeamGlobals.isPasswordValid(requirePasswordInput.text)) {
                requirePasswordInput.forceActiveFocus(Qt.TabFocusReason);
                requirePasswordInput.selectAll();
                requirePasswordError.text = qsTrId("general-pwd-invalid");
                return;
            }
        }

        accepted();
        close();
    }

    function openHandler() {
        defaultFocusItem.forceActiveFocus(Qt.TabFocusReason);
    }

    function passworInputEnter() {
        okButton.forceActiveFocus(Qt.TabFocusReason);
        okButton.clicked();
    }

    function getFeeInSecondCurrency(feeValue) {
        return BeamGlobals.calcFeeInSecondCurrency(
            feeValue,
            sendViewConfirm.currency,
            sendViewConfirm.secondCurrencyRate,
            sendViewConfirm.secondCurrencyLabel)
    }

    function getAmountInSecondCurrency() {
        return BeamGlobals.calcAmountInSecondCurrency(
            sendViewConfirm.amount,
            sendViewConfirm.currency,
            sendViewConfirm.secondCurrencyRate,
            sendViewConfirm.secondCurrencyLabel)
    }

    onAccepted: {
        onAcceptedCallback();
    }

    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing:      30

            SFText {
                id: title
                Layout.alignment: Qt.AlignHCenter
                Layout.minimumHeight: 21
                Layout.leftMargin: 68
                Layout.rightMargin: 68
                Layout.topMargin: 14
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight: Font.Bold
                color: Style.content_main
                text: sendViewConfirm.swapMode ?
                    //% "Confirm atomic swap"
                    qsTrId("send-swap-confirmation-title") :
                    //% "Confirm transaction details"
                    qsTrId("send-confirmation-title")
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 25
                columnSpacing: 14
                rowSpacing: 12
                columns: 2
                rows: 5

                //
                // Recipient/Address
                //
                SFText {
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    Layout.minimumHeight: 16
                    font.pixelSize: 14
                    color: Style.content_disabled
                    //% "Recipient"
                    text: qsTrId("send-confirmation-recipient-label") + ":"
                    verticalAlignment: Text.AlignTop
                }

                SFText {
                    id: addressLabel
                    Layout.fillWidth: true
                    Layout.maximumWidth: 290
                    Layout.minimumHeight: 16
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    font.pixelSize: 14
                    color: Style.content_main
                }

                //
                // Amount
                //
                SFText {
                    Layout.row: 2
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    Layout.minimumHeight: 16
                    // Layout.bottomMargin: 3
                    Layout.topMargin: 9
                    font.pixelSize: 14
                    color: Style.content_disabled
                    //% "Amount"
                    text: qsTrId("general-amount") + ":"
                    verticalAlignment: Text.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    SFText {
                        id: amountLabel
                        Layout.minimumHeight: 29
                        font.pixelSize: 24
                        color: Style.accent_outgoing
                        text: [
                                Utils.uiStringToLocale(sendViewConfirm.amount),
                                sendViewConfirm.currencyLabel
                            ].join(" ")
                    }
                    SFText {
                        id: secondCurrencyAmountLabel
                        font.pixelSize: 14
                        color: Style.content_disabled
                        text: getAmountInSecondCurrency()
                    }
                }

                //
                // Fee
                //
                SFText {
                    Layout.row: 3
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    Layout.minimumHeight: 16
                    font.pixelSize: 14
                    color: Style.content_disabled
                    text: sendViewConfirm.feeLabel
                    verticalAlignment: Text.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    SFText {
                        id: feeLabel
                        Layout.minimumHeight: 16
                        font.pixelSize: 14
                        color: Style.content_main
                        text: [
                                Utils.uiStringToLocale(sendViewConfirm.fee),
                                BeamGlobals.getFeeRateLabel(sendViewConfirm.currency)
                            ].join(" ")
                    }
                    SFText {
                        id: secondCurrencyFeeLabel
                        font.pixelSize: 14
                        color: Style.content_disabled
                        text: getFeeInSecondCurrency(parseInt(sendViewConfirm.fee, 10))
                    }
                }

                //
                // Password confirmation
                //
                SFText {
                    id: requirePasswordLabel
                    visible: BeamGlobals.needPasswordToSpend()
                    Layout.row: 4
                    Layout.columnSpan: 2
                    Layout.topMargin: 50
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    Layout.minimumHeight: 16
                    font.pixelSize: 14
                    color: Style.content_main
                    //% "To broadcast your transaction please enter your password"
                    text: qsTrId("send-confirmation-pwd-require-message")
                }

                SFTextInput {
                    id: requirePasswordInput
                    visible: BeamGlobals.needPasswordToSpend()
                    Layout.row: 5
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    focus: true
                    activeFocusOnTab: true
                    font.pixelSize: 14
                    color: Style.content_main
                    echoMode: TextInput.Password
                    onAccepted: passworInputEnter()
                    onTextChanged: if (requirePasswordError.text.length > 0) requirePasswordError.text = ""
                }

                SFText {
                    id: requirePasswordError
                    visible: BeamGlobals.needPasswordToSpend()
                    Layout.row: 6
                    Layout.columnSpan: 2
                    height: 16
                    width: parent.width
                    color: Style.validator_error
                    font.pixelSize: 14
                }

                //
                // Wait online message
                //
                SFText {
                    Layout.row: 7
                    Layout.columnSpan: 2
                    Layout.topMargin: 15
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: sendViewConfirm.swapMode
                    Layout.maximumHeight:  60
                    Layout.maximumWidth: sendViewConfirm.swapMode ? parent.width : 400
                    Layout.minimumHeight: 16
                    font.pixelSize: 14
                    color: Style.content_disabled
                    wrapMode: Text.WordWrap
                    text: sendViewConfirm.swapMode ?
                        //% "Keep your wallet online. The swap normally takes about 1 hour to complete."
                        qsTrId("send-swap-sconfirmation-online-time") + (sendViewConfirm.currency !== Currency.CurrBeam ?
                            //% " Once the offer is accepted by the other side, the %1 transaction fee will be charged even if the offer is cancelled."
                            qsTrId("send-swap-fee-warning").arg(sendViewConfirm.currencyLabel)
                            : "")
                        :
                        //% "For the transaction to complete, the recipient must get online within the next 12 hours and you should get online within 2 hours afterwards."
                        qsTrId("send-confirmation-pwd-text-online-time")
                }
            }
        }
    }
}