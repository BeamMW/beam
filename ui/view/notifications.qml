import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0

Item {
    id: notificationsViewRoot
    anchors.fill: parent

    NotificationsViewModel {
        id: viewModel
    }

    RowLayout {
        Title {
            //% "Notifications"
            text: qsTrId("notifications-title")
        }
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    CustomButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 49

        height: 38
        palette.button: Style.background_second
        palette.buttonText : Style.content_main
        icon.source: "qrc:/assets/icon-cancel-white.svg"
        //% "clear all"
        text: qsTrId('notifications-clear-all')
        visible: viewModel.readNotifications.rowCount() + viewModel.unreadNotifications.rowCount() > 0
        onClicked: {
            console.log('TODO: clear all notifications')
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.topMargin: 97
        clip: true
        id:scrl

        ColumnLayout {
            width: scrl.availableWidth
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: viewModel.unreadNotifications

                    Item {
                        Layout.fillWidth: true
                        Layout.minimumHeight: 121

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
            }

            SFText {
                Layout.alignment: Qt.AlignHCenter

                //% "Read"
                text: qsTrId('notifications-read').toUpperCase()

                font.pixelSize: 14
                color: Style.content_main
                visible: viewModel.readNotifications.rowCount() > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                visible: viewModel.readNotifications.rowCount() > 0

                Repeater {
                    model: viewModel.readNotifications

                    Item {
                        Layout.fillWidth: true
                        Layout.minimumHeight: 121

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
