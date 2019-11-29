import QtQuick.Controls 2.4
import "controls"

ConfirmationDialog {
    parent: Overlay.overlay

    //% "Can't send to the expired address."
    text: qsTrId("cant-send-to-expired-message")

    //% "Ok"
    okButtonText:        qsTrId("general-ok")
    okButtonIconSource:  "qrc:/assets/icon-done.svg"
    cancelButtonVisible: false
}