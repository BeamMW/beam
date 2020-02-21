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

    CustomButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 49

        height: 38
        palette.button: Style.background_second
        palette.buttonText : Style.content_main
        icon.source: "qrc:/assets/icon-cancel-white.svg"
        text: 'clear all'
    }

    ListView {
        id: notificationsList

        anchors.fill: parent
        anchors.topMargin: 97
        spacing: 10
        clip: true

        model: viewModel.notifications
        // ListModel {
            // ListElement {
            //     type: "update"
            //     title: "New version v1.2.3 is avalable"
            //     message: "Your current version is v1.2.2. Please update to get the most of your Beam wallet."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "expired"
            //     title: "Address expired"
            //     message: "167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb address expired."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "received"
            //     title: "Transaction received"
            //     message: "You received 1234 BEAM from 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "sent"
            //     title: "Transaction sent"
            //     message: "You sent 1234 BEAM to 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "failed"
            //     title: "Transaction failed"
            //     message: "Sending 1234 BEAM to 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb failed."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "inpress"
            //     title: "BEAM in the press"
            //     message: "Beam: Halved Successfully, Plans For 2020, Lelantus MW Discussed, Progress On Double Doppler 4.2, Beam Web Wallet And Trustless Wallet Service. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "hotnews"
            //     title: "BEAN Hot News"
            //     message: "Double Doppler 4.0 — Atomic Swaps — Release Notes. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "videos"
            //     title: "BEAM Videos & Podcasts"
            //     message: "Beam Weekly Development Update 21 January 2020. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "events"
            //     title: "BEAM Events"
            //     message: "Advancing Bitcoin Developer Conference 2020. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "newsletter"
            //     title: "BEAM Newletter"
            //     message: "Beam 2020 Week #4. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }

            // ListElement {
            //     type: "community"
            //     title: "BEAM Community"
            //     message: "My Dialogue with Agbona Igwemoh, Africa’s Lead Ambassador for Beam. Please visit News Center on Beam website for more information."
            //     date: "25.01.2020   |   1:20 PM"
            // }
        // }

        delegate: Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 121

            Rectangle {
                radius: 10
                anchors.fill: parent
                color: Style.background_popup
            }

            Row {
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
                        text: date
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
    } // ListView

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

        return icons[notificationType]
    }

} // Item
