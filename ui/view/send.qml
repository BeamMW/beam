import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

ColumnLayout {
    id: sendView
    property bool   isSwapMode: false
    property bool   isSwapOnFly: false
    property bool   pasteEventComplete: false
    property var    defaultFocusItem: receiverTAInput
    property bool   isErrorDetected: false

    // callbacks set by parent
    property var    onClosed: function() {}
    property var    onSwapToken: function() {}
    property var    onAddress: function() {}

    TopGradient {
        mainRoot: main
        topColor: Style.accent_outgoing
    }

    Row {
        Layout.alignment:    Qt.AlignHCenter
        Layout.topMargin:    75
        Layout.bottomMargin: 40

        SFText {
            font.pixelSize:  18
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            text:            isSwapMode || isSwapOnFly
                                        //% "Swap currencies"
                                        ? qsTrId("wallet-send-swap-title")
                                        //% "Send"
                                        : qsTrId("send-title")
        }
    }

    function isTAInputValid(token) {
        return token.length == 0 || BeamGlobals.isTAValid(token)
    }

    ColumnLayout {
        
        SFText {
            font.pixelSize:  14
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            //% "Transaction token or contact"
            text:            qsTrId("send-send-to-label")
        }

        SFTextInput {
            Layout.fillWidth: true
            id:               receiverTAInput
            font.pixelSize:   14
            color:            isErrorDetected
                ? Style.validator_error
                : Style.content_main
            backgroundColor:  isErrorDetected
                ? Style.validator_error
                : Style.content_main
            validator:        RegExpValidator { regExp: /[0-9a-zA-Z]{1,}/ }
            selectByMouse:    true
            //% "Please specify contact or transaction token"
            placeholderText:  qsTrId("send-contact-placeholder")
            font.italic:      isErrorDetected
            onPaste: function() {
                pasteEventComplete = true;
            }

            onTextChanged: {
                isErrorDetected = false;
                isSwapOnFly = false;
                if (receiverTAInput.text.length == 0) {
                    pasteEventComplete = false;
                    return;
                }

                if (!isSwapMode) {
                    isSwapOnFly = !BeamGlobals.isAddress(receiverTAInput.text);
                }

                if (!isTAInputValid(receiverTAInput.text)) {
                    isSwapOnFly = false;
                    isErrorDetected = true;
                    pasteEventComplete = false;
                    return;
                }

                isErrorDetected = (isSwapOnFly || isSwapMode) &&
                        !BeamGlobals.isSwapToken(receiverTAInput.text);
                
                if (isErrorDetected) {
                    pasteEventComplete = false;
                    return;
                }

                if (pasteEventComplete) {
                    actionButton.onClicked();
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
                visible:          isErrorDetected
                text:             isSwapMode || isSwapOnFly
                    //% "Invalid swap token"
                    ? qsTrId("wallet-send-invalid-token")
                    //% "Invalid wallet address or swap token"
                    : qsTrId("wallet-send-invalid-address-or-token")
            }
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
                    onClosed();
                }
            }
            
            CustomButton {
                id: actionButton
                text: isSwapMode || isSwapOnFly
                    //% "Swap"
                    ? qsTrId("general-swap")
                    //% "Send"
                    : qsTrId("general-send")
                palette.buttonText: Style.content_opposite
                palette.button: Style.accent_outgoing
                icon.source: "qrc:/assets/icon-send-blue.svg"
                enabled: !isErrorDetected && receiverTAInput.text.length
                onClicked: {
                    isSwapMode || isSwapOnFly
                        ? onSwapToken(receiverTAInput.text)
                        : onAddress(receiverTAInput.text);
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }    
}
