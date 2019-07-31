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
    property var  defaultFocusItem: null

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

    property var currentView: null

    Component.onCompleted: {
        currentView            = Qt.createComponent("send_regular.qml").createObject(thisView)
        currentView.parentView = thisView
        defaultFocusItem       = currentView.defaultFocusItem
    }

    function onSwapToken(token) {
        currentView.destroy();
        currentView            = Qt.createComponent("send_swap.qml").createObject(thisView);
        currentView.parentView = thisView
        currentView.setToken(token)
    }
}