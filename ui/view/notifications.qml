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

        // Rectangle {
        //     anchors.fill: parent
        //     color: "#ff0000"
        //     opacity: 0.1
        // }

        anchors.fill: parent
        anchors.topMargin: 97
        spacing: 10
        clip: true

        model: ListModel {
            ListElement {
                type: "update"
                title: "New version v1.2.3 is avalable"
                message: "Your current version is v1.2.2. Please update to get the most of your Beam wallet."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "expired"
                title: "Address expired"
                message: "167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb address expired."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "received"
                title: "Transaction received"
                message: "You received 1234 BEAM from 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "sent"
                title: "Transaction sent"
                message: "You sent 1234 BEAM to 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "failed"
                title: "Transaction failed"
                message: "Sending 1234 BEAM to 167jbfsdjkflk39902mnsdnkbkjadavfd39nas7877qwb failed."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "inpress"
                title: "BEAM in the press"
                message: "Beam: Halved Successfully, Plans For 2020, Lelantus MW Discussed, Progress On Double Doppler 4.2, Beam Web Wallet And Trustless Wallet Service. Please visit News Center on Beam website for more information."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "hotnews"
                title: "BEAN Hot News"
                message: "Double Doppler 4.0 — Atomic Swaps — Release Notes. Please visit News Center on Beam website for more information."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "videos"
                title: "BEAM Videos & Podcasts"
                message: "Beam Weekly Development Update 21 January 2020. Please visit News Center on Beam website for more information."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "events"
                title: "BEAM Events"
                message: "Advancing Bitcoin Developer Conference 2020. Please visit News Center on Beam website for more information."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "newsletter"
                title: "BEAM Newletter"
                message: "Beam 2020 Week #4. Please visit News Center on Beam website for more information."
                date: "25.01.2020   |   1:20 PM"
            }

            ListElement {
                type: "community"
                title: "BEAM Community"
                message: "My Dialogue with Agbona Igwemoh, Africa’s Lead Ambassador for Beam. Please visit News Center on Beam website for more information."
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
            case "active":
                return "qrc:/assets/icon-notifications-active.svg";
            case "community":
                return "qrc:/assets/icon-notifications-community.svg";
            case "events":
                return "qrc:/assets/icon-notifications-events.svg";
            case "expired":
                return "qrc:/assets/icon-notifications-expired.svg";
            case "failed":
                return "qrc:/assets/icon-notifications-failed.svg";
            case "hotnews":
                return "qrc:/assets/icon-notifications-hotnews.svg";
            case "inpress":
                return "qrc:/assets/icon-notifications-inpress.svg";
            case "newsletter":
                return "qrc:/assets/icon-notifications-newsletter.svg";
            case "received":
                return "qrc:/assets/icon-notifications-received.svg";
            case "sent":
                return "qrc:/assets/icon-notifications-sent.svg";
            case "update":
                return "qrc:/assets/icon-notifications-update.svg";
            case "videos":
                return "qrc:/assets/icon-notifications-videos.svg";

            default: return "";
        }
    }
} // Item
