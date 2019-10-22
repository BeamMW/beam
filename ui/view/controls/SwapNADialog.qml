import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import "."

ConfirmationDialog {
    id:                       changeSettingsDialog
    okButtonColor:            Style.active
    okButtonText:             qsTrId("general-change-settings")
    okButtonIconSource:       "qrc:/assets/icon-settings-blue.svg"
    cancelButtonIconSource:   "qrc:/assets/icon-cancel-white.svg"
    closePolicy:              Popup.NoAutoClose
    property alias text:      message.text

    function openHandler() {
        okButton.forceActiveFocus(Qt.TabFocusReason);
    }

    contentItem: Item {
        id: confirmationContent
        Column {
            anchors.fill: parent
            spacing: 30

            SFText {
                width:                parent.width
                topPadding:           20
                font.pixelSize:       18
                color:                Style.content_main
                horizontalAlignment:  Text.AlignHCenter
                //% "Swap"
                text:                 qsTrId("general-swap")
            }

            SFText {
                id:                   message
                width:                parent.width
                leftPadding:          20
                rightPadding:         20
                bottomPadding:        30
                font.pixelSize:       14
                color:                Style.content_main
                wrapMode:             Text.Wrap
                horizontalAlignment:  Text.AlignHCenter
            }
        }
    }
}
