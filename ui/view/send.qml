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
    property bool regularMode: true
    property var  defaultFocusItem: receiverTAInput

    Row {
        Layout.alignment:    Qt.AlignHCenter
        Layout.topMargin:    75
        Layout.bottomMargin: 40

        SFText {
            font.pixelSize:  18
            font.styleName:  "Bold"; font.weight: Font.Bold
            color:           Style.content_main
            text:            regularMode ? qsTrId("send-title") : qsTrId("wallet-send-swap-title") //% "Send Beam" / "Swap"
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
            text:            qsTrId("send-send-to-label") //% "Transaction token or contact"
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
            placeholderText:  qsTrId("send-contact-placeholder") //% "Please specify contact"

            onTextChanged: {
                if (!isTAInputValid()) return;
                BeamGlobals.isSwapToken(receiverTAInput.text) ? onInitialSwapToken(receiverTAInput.text) : onInitialAddress(receiverTAInput.text);
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

        Item {
            Layout.fillHeight: true
        }
    }

    property var currentView: undefined

    function onSwapToken(token) {
        if (currentView) currentView.destroy()
        currentView            = Qt.createComponent("send_swap.qml").createObject(thisView);
        currentView.parentView = thisView
        currentView.setToken(token)
        regularMode = false
    }

    function onInitialSwapToken(token) {
        if (currentView) currentView.destroy()
        currentView            = Qt.createComponent("send_swap.qml").createObject(thisView);
        currentView.parentView = thisView
        currentView.setToken(token)
        regularMode = false
    }

    function onInitialAddress(address) {
        if (currentView) currentView.destroy()
        currentView            = Qt.createComponent("send_regular.qml").createObject(thisView)
        currentView.parentView = thisView
        defaultFocusItem       = currentView.defaultFocusItem
        currentView.setToken(address)
        regularMode = true
    }
}