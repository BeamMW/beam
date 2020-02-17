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
    Layout.fillWidth: true
    Layout.fillHeight: true

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

    ListView {
        id: notificationsList

        y: 97
        Layout.fillWidth : true
        Layout.fillHeight : true

        model: ListModel {
            ListElement {
                type: "test"
                title: "New version v1.2.3 is avalable"
                message: "Your current version is v1.2.2. Please update to get the most of your Beam wallet."
                date: "25.01.2020   |   1:20 PM"
            }
            ListElement {
                type: "test"
                title: "News title"
                message: "News message content"
                date: "25.01.2020   |   1:20 PM"
            }
        }

        delegate: Item {
            width: 914
            height: 121

            Rectangle {
                radius: 10
                anchors.fill: parent
                color: Style.background_popup
            }
            RowLayout {
                anchors.fill: parent
                spacing: 30
                SvgImage {
                    Layout.leftMargin: 30
                    Layout.maximumHeight: 40
                    Layout.maximumWidth: 40
                    source: getIconSource(type)
                }
                ColumnLayout {
                    SFText {
                        text: title
                    }
                    SFText {
                        text: message
                    }
                    SFText {
                        text: date
                    }
                }
                ColumnLayout {
                    CustomToolButton {
                        icon.source: "qrc:/assets/icon-cancel-white.svg"
                    }
                    CustomButton {
                        height: 38
                        palette.button: Style.background_second
                        palette.buttonText : Style.content_main
                        icon.source: "qrc:/assets/icon-repeat-white.svg"
                    }
                }
            }
        }
    } // ListView

    function getIconSource(notificationType) {
        switch(notificationType)
        {
            case "test":
                return "qrc:/assets/icon-notifications-update.svg";
            default: return "";
        }
    }
} // Item
