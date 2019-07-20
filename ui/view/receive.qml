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

    Component.onDestruction: {
       if (regularMode) receive.saveAddress()
       else swap.saveAddress()
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
            //% "Receive" / "Create swap offer"
            text:                regularMode ? qsTrId("wallet-receive-title") : qsTrId("wallet-swap-title")
        }

        CustomSwitch {
            id:   mode
            text: qsTrId("wallet-swap")
            x:    parent.width - width
        }

        Binding {
            target:   thisView
            property: "regularMode"
            value:    !mode.checked
        }
    }

    ReceiveRegular {
        id:               receive
        visible:          regularMode
    }

    ReceiveSwap {
        id:               swap
        visible:          !regularMode
    }

    Item {
        Layout.fillHeight: true
    }
}
