import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

ColumnLayout {
    id: thisView
    property bool   isSwapMode: false
    property var    defaultFocusItem: receiverTAInput
    property var    predefinedTxParams: undefined

    Component.onCompleted: {
        if (isSwapMode) {
            onSwapToken("");
        }
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

    function isTAInputValid() {
        return receiverTAInput.text.length == 0 || BeamGlobals.isTAValid(receiverTAInput.text)
    }

    ColumnLayout {
        visible: currentView === undefined

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
            color:            isTAInputValid() ? Style.content_main : Style.validator_error
            backgroundColor:  isTAInputValid() ? Style.content_main : Style.validator_error
            font.italic :     !isTAInputValid()
            validator:        RegExpValidator { regExp: /[0-9a-zA-Z]{1,}/ }
            selectByMouse:    true
            //% "Please specify contact or transaction token"
            placeholderText:  qsTrId("send-contact-placeholder")

            onTextChanged: {
                if (!isTAInputValid()) return;
                if (receiverTAInput.text.length == 0) return;
                BeamGlobals.isSwapToken(receiverTAInput.text) ? onSwapToken(receiverTAInput.text) : onAddress(receiverTAInput.text);
            }
        }

        Item {
            Layout.fillWidth: true
            SFText {
                Layout.alignment: Qt.AlignTop
                id:               receiverTAError
                color:            Style.validator_error
                font.pixelSize:   12
                //% "Invalid address or token"
                text:             qsTrId("wallet-send-invalid-token")
                visible:          !isTAInputValid()
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
                onClicked:          walletView.pop();
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    property var currentView: undefined

    function onSwapToken(token) {
        if (currentView) currentView.destroy()
        currentView            = Qt.createComponent("send_swap.qml").createObject(thisView, {"predefinedTxParams": predefinedTxParams});
        currentView.parentView = thisView
        currentView.setToken(token)
        isSwapMode = true
    }

    function onAddress(address) {
        if (currentView) currentView.destroy()
        currentView            = Qt.createComponent("send_regular.qml").createObject(thisView)
        currentView.parentView = thisView
        defaultFocusItem       = currentView.defaultFocusItem
        currentView.setToken(address)
        isSwapMode = false
    }

    function onBadSwap() {
        if (currentView) {
            currentView.destroy()
            receiverTAInput.text = ""
            currentView = undefined
        }
    }
}