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
    property bool   pasteEventComplete: false
    property var    defaultFocusItem: receiverTAInput

    // callbacks set by parent
    property var    onClosed: undefined
    property var    onSwapToken: undefined
    property var    onAddress: undefined

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
            text:            isSwapMode
                                        //% "Swap currencies"
                                        ? qsTrId("wallet-send-swap-title")
                                        //% "Send"
                                        : qsTrId("send-title")
        }
    }

    function isTAInputValid(token) {
        return token.length == 0 || BeamGlobals.isTAValid(token)
    }

    function setInputErrorMode() {
        receiverTAInput.backgroundColor =
            receiverTAInput.color = Style.validator_error;
        receiverTAError.text = isSwapMode
            //% "Invalid token"
            ? qsTrId("wallet-send-invalid-token")
            //% "Invalid address or token"
            : qsTrId("wallet-send-invalid-address-or-token");
        receiverTAError.visible =
            receiverTAInput.font.italic = true;
    }

    function setInputNormalMode() {
        receiverTAInput.backgroundColor =
            receiverTAInput.color = Style.content_main;
        receiverTAError.visible =
            receiverTAInput.font.italic = false;
    }

    function checkToken(token) {
        if (!isTAInputValid(token)) {
            setInputErrorMode();
            actionButton.enabled = false;
            return;
        }

        if (BeamGlobals.isSwapToken(receiverTAInput.text) &&
            typeof onSwapToken == "function") {
            isSwapMode = true;
            onSwapToken(receiverTAInput.text);
            return;
        } else if (isSwapMode) {
            setInputErrorMode();
        }

        if (typeof onAddress == "function") {
            onAddress(receiverTAInput.text);
        }
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
            color:            Style.content_main
            backgroundColor:  Style.content_main
            validator:        RegExpValidator { regExp: /[0-9a-zA-Z]{1,}/ }
            selectByMouse:    true
            //% "Please specify contact or transaction token"
            placeholderText:  qsTrId("send-contact-placeholder")

            Keys.onPressed: function(keyEvent) {
                pasteEventComplete = keyEvent.matches(StandardKey.Paste);
            }

            onTextChanged: {
                setInputNormalMode();
                if (receiverTAInput.text.length == 0) {
                    actionButton.enabled = false;
                    return;
                }
                actionButton.enabled = true;

                if (pasteEventComplete) {
                    checkToken(receiverTAInput.text);
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
                visible:          false
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
                    if (typeof onClosed == "function") {
                        onClosed();
                    }
                }
            }
            
            CustomButton {
                id: actionButton
                text: isSwapMode
                    //% "Swap"
                    ? qsTrId("general-swap")
                    //% "Send"
                    : qsTrId("general-send")
                palette.buttonText: Style.content_opposite
                palette.button: Style.accent_outgoing
                icon.source: "qrc:/assets/icon-send-blue.svg"
                enabled: false
                onClicked: checkToken(receiverTAInput.text)
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }    
}