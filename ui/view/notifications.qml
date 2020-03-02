import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: notificationsViewRoot
    anchors.fill: parent

    NotificationsViewModel {
        id: viewModel
    }

    ConfirmationDialog {
        id:                     clearAllDialog
        //% "Clear all notifications"
        title:                  qsTrId("notifications-clear-all-dialog")
        //% "Are you sure you want to remove all notifications?"
        text:                   qsTrId("notifications-clear-all-text")
        //% "yes"
        okButtonText:           qsTrId("notifications-clear-all-yes-button")
        okButtonIconSource:     "qrc:/assets/icon-done.svg"
        //% "no"
        cancelButtonText:       qsTrId("notifications-clear-all-no-button")
        cancelButtonIconSource: "qrc:/assets/icon-cancel-16.svg"
        onAccepted: {
            console.log('TODO: clear all notifications')
            //viewModel.clearAll();
        }
    }

    Title {
        //% "Notifications"
        text: qsTrId("notifications-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    CustomButton {
        Layout.alignment: Qt.AlignRight | Qt.AlignTop

        height: 38
        palette.button: Style.background_second
        palette.buttonText : Style.content_main
        icon.source: "qrc:/assets/icon-cancel-white.svg"
        //% "clear all"
        text: qsTrId('notifications-clear-all')
        visible: viewModel.notifications.rowCount() > 0
        onClicked: {
            clearAllDialog.open();
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        model: SortFilterProxyModel {
            source: SortFilterProxyModel {
                source: viewModel.notifications
                sortOrder: Qt.DescendingOrder
                sortCaseSensitivity: Qt.CaseInsensitive
                sortRole: "timeCreatedSort"
            }
            sortOrder: Qt.DescendingOrder
            sortCaseSensitivity: Qt.CaseInsensitive
            sortRole: "state"
        }
        spacing: 10
        clip: true

        ScrollBar.vertical: ScrollBar { }
        section.property: "state"
        section.delegate: Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 24
            SFText {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                text: section
                font.pixelSize: 12
                color: Style.section
                font.capitalization: Font.AllUppercase
            }
        }

        delegate: Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 121
        
            Rectangle {
                radius: 10
                anchors.fill: parent
                color: Style.background_popup
            }
        
            Item {
                anchors.fill: parent
        
                SvgImage {
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    anchors.verticalCenter: parent.verticalCenter 
        
                    source: getIconSource(type)
                }
        
                Column {
                    anchors.fill: parent
                    anchors.topMargin: 20
                    anchors.leftMargin: 100
                    anchors.rightMargin: 150
                    clip: true
        
                    spacing: 10
        
                    SFText {
                        text: title
                        font.pixelSize: 18
                        color: Style.content_main
                        font.styleName: "Bold"; font.weight: Font.Bold
                    }
        
                    SFText {
                        text: message
                        font.pixelSize: 14
                        color: Style.content_main
                    }
        
                    SFText {
                        text: timeCreated
                        font.pixelSize: 12
                        color: Style.content_main
                        opacity: 0.5
                    }
                }
            }
        
            CustomToolButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 20
                anchors.rightMargin: 20
        
                icon.source: "qrc:/assets/icon-cancel-white.svg"
            }
        
            CustomButton {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.bottomMargin: 20
                anchors.rightMargin: 20
        
                height: 38
                palette.button: Style.background_second
                palette.buttonText : Style.content_main
                icon.source: getActionButtonIcon(type)
                text: getActionButtonLabel(type)
        
                visible: getActionButtonLabel(type) != undefined
            }
        }
    }

    function getIconSource(notificationType) {
        return "qrc:/assets/icon-notifications-" + notificationType;
    }

    function getActionButtonLabel(notificationType) {
        //% "update now"
        var updateLabel = qsTrId('notifications-update-now')

        //% "activate"
        var activateLabel = qsTrId('notifications-activate')

        //% "details"
        var detailsLabel = qsTrId('notifications-details')

        var labels =
        {
            update: updateLabel,
            expired: activateLabel,
            received: detailsLabel,
            sent: detailsLabel,
            failed: detailsLabel
        }

        return labels[notificationType]
    }

    function getActionButtonIcon(notificationType) {
        var updateIcon = 'qrc:/assets/icon-repeat-white.svg'

        var icons =
        {
            update: updateIcon,
            expired: updateIcon,
            received: updateIcon,
            sent: updateIcon,
            failed: updateIcon
        }

        return icons[notificationType] || ''
    }

} // Item
