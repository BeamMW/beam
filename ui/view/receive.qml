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
    
    property bool isSwapMode: false
    property bool isSwapOnly
    property var defaultFocusItem: null
    property var currentView: null

    Component.onCompleted: {
        if (!currentView) {
            createChild();
        }
        isSwapOnly = isSwapMode;
    }

    Component.onDestruction: {
        if (!isSwapMode) currentView.saveAddress();
    }

    onIsSwapModeChanged: {
        createChild();
        if (isSwapMode && !BeamGlobals.canSwap()) swapna.open();

    }

    SwapNADialog {
        id: swapna
        onRejected: thisView.isSwapMode = false
        onAccepted: main.openSwapSettings()
        //% "You do not have any 3rd-party currencies connected.\nUpdate your settings and try again."
        text:       qsTrId("swap-na-message").replace("\\n", "\n")
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
            text:                isSwapMode
                                            //% "Create swap offer"
                                            ? qsTrId("wallet-receive-swap-title")
                                            //% "Receive"
                                            : qsTrId("wallet-receive-title")
        }

        CustomSwitch {
            id:         mode
            //% "Swap"
            text:       qsTrId("wallet-swap")
            x:          parent.width - width
            checked:    isSwapMode
            visible:    !isSwapOnly
        }

        Binding {
            target:     thisView
            property:   "isSwapMode"
            value:      mode.checked
        }
    }

    function createChild() {
        if (currentView) currentView.destroy();
        currentView       = Qt.createComponent(isSwapMode ? "receive_swap.qml" : "receive_regular.qml").createObject(thisView)
        defaultFocusItem  = currentView.defaultFocusItem
        currentView.defaultFocusItem.forceActiveFocus()
    }

    Item {
        Layout.fillHeight: true
    }
}
