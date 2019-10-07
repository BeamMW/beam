import QtQuick.Controls 2.4
import "controls"

ConfirmationDialog {
    id: onlySwapTokenAllowed
    parent: Overlay.overlay

    //% "Only swap token is allowed to use here."
    text: qsTrId("only-swap-token-allowed")

    //% "Ok"
    okButtonText:        qsTrId("general-ok")
    okButtonIconSource:  "qrc:/assets/icon-done.svg"
    cancelButtonVisible: false
}