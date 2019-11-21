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
    property var  defaultFocusItem: receiverTAInput
    property bool isValid: !receiverTAInput.text || BeamGlobals.isSwapToken(receiverTAInput.text)

    // callbacks set by parent
    property var    onClosed:    function() {}
    property var    onSwapToken: function() {}

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
            //% "Accept Swap Offer"
            text:            qsTrId("wallet-send-swap-title")
        }
    }

    ColumnLayout {
        SFText {
            font.pixelSize:  14
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            //% "Swap token"
            text:            qsTrId("send-swap-token")
        }

        SFTextInput {
            Layout.fillWidth: true
            id:               receiverTAInput
            font.pixelSize:   14
            color:            isValid ? Style.content_main : Style.validator_error
            backgroundColor:  isValid ? Style.content_main : Style.validator_error
            font.italic:      !isValid
            validator:        RegExpValidator { regExp: /[0-9a-zA-Z]{1,}/ }
            selectByMouse:    true
            //% "Paste token here"
            placeholderText:  qsTrId("send-swap-token-hint")

            onTextPasted: {
                if (BeamGlobals.isSwapToken(text)) {
                    onSwapToken(text)
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
                font.italic:      true
                visible:          !isValid
                //% "Invalid swap token"
                text:             qsTrId("wallet-send-invalid-token")
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
                onClicked:          onClosed()
            }
            
            CustomButton {
                id:                actionButton
                //% "Swap"
                text:               qsTrId("general-swap")
                palette.buttonText: Style.content_opposite
                palette.button:     Style.accent_outgoing
                icon.source:        "qrc:/assets/icon-send-blue.svg"
                enabled:            receiverTAInput.text && isValid
                onClicked:          onSwapToken(receiverTAInput.text)
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }    
}
