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
    
    property bool isSwapView: false
    property bool isSwapOnly
    property var defaultFocusItem: null
    property var currentView: null

    Component.onCompleted: {
        if (!currentView) {
            createChild();
        }
        isSwapOnly = isSwapView;
    }

    Component.onDestruction: {
        if (!isSwapView) currentView.saveAddress();
    }

    onIsSwapViewChanged: {
        createChild();
        if (isSwapView && !BeamGlobals.canSwap()) {
            thisView.enabled = false;
            swapna.open();
        }
    }

    SwapNADialog {
        id: swapna

        onRejected: {
            thisView.enabled = true
            thisView.regularMode = true
        }

        onAccepted: {
            thisView.enabled = true
            main.openSwapSettings()
        }
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
            text:                isSwapView ? qsTrId("wallet-receive-swap-title") : qsTrId("wallet-receive-title")
        }

        CustomSwitch {
            id:   mode
            text: qsTrId("wallet-swap")
            x:    parent.width - width
            checked: isSwapView
            enabled: !isSwapOnly
        }

        Binding {
            target:   thisView
            property: "isSwapView"
            value:    mode.checked
        }
    }

    function createChild() {
        if (currentView) currentView.destroy();
        currentView       = Qt.createComponent(isSwapView ? "receive_swap.qml" : "receive_regular.qml").createObject(thisView)
        defaultFocusItem  = currentView.defaultFocusItem
        currentView.defaultFocusItem.forceActiveFocus()
    }

    Item {
        Layout.fillHeight: true
    }
}
