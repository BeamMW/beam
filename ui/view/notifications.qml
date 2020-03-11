import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.4
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: notificationsViewRoot
    anchors.fill: parent
    spacing: 0

    NotificationsViewModel {
        id: viewModel
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
        Layout.preferredHeight: 38
        Layout.bottomMargin: 10
        palette.button: Style.background_second
        palette.buttonText : Style.content_main
        icon.source: "qrc:/assets/icon-cancel-white.svg"
        //% "clear all"
        text: qsTrId('notifications-clear-all')
        visible: notificationList.model.count > 0
        onClicked: {
            viewModel.clearAll();
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: notificationList.model.count == 0

        SvgImage {
            Layout.topMargin: 100
            Layout.alignment: Qt.AlignHCenter
            source:     "qrc:/assets/icon-notifications.svg"
            sourceSize: Qt.size(60, 60)
        }

        SFText {
            Layout.topMargin:     30
            Layout.alignment:     Qt.AlignHCenter
            horizontalAlignment:  Text.AlignHCenter
            font.pixelSize:       14
            color:                Style.content_main
            opacity:              0.5
            lineHeight:           1.43
            /*% "There are no notifications yet."*/
            text:                 qsTrId("notifications-empty")
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    ListView {
        id: notificationList
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: notificationList.model.count > 0 
        boundsMovement: Flickable.StopAtBounds
        boundsBehavior: Flickable.StopAtBounds

        model: SortFilterProxyModel {
            source: viewModel.notifications
            sortOrder: Qt.DescendingOrder
            sortCaseSensitivity: Qt.CaseInsensitive
            sortRole: "timeCreatedSort"
        }
        spacing: 10
        clip: true

        //! [transitions]
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1.0; easing.type: Easing.InOutQuad }
        }
        remove: Transition {
            NumberAnimation { property: "opacity"; to: 0; easing.type: Easing.InOutQuad }
        }
        
        displaced: Transition {
            SequentialAnimation {
                PauseAnimation { duration: 125 }
                NumberAnimation { property: "y"; easing.type: Easing.InOutQuad }
            }
        }
        //! [transitions]

        ScrollIndicator.vertical: ScrollIndicator { }

        section.property: "state"
        section.delegate: Item {
            anchors.left: parent.left
            anchors.right: parent.right
            property bool isRead: section == "read" 
            height: isRead ? 24 : 0
            
            SFText {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                //% "read"
                text: qsTrId("notifications-read")
                font.pixelSize: 12
                color: Style.section
                font.capitalization: Font.AllUppercase
                visible: parent.isRead
            }
        }

        delegate: Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 121
        
            property bool isUnread: model.state == "unread" 

            Rectangle {
                radius: 10
                anchors.fill: parent
                color: (parent.isUnread) ? Style.active : Style.background_second
                opacity: (parent.isUnread) ? 0.1 : 1.0
            }

            Timer {
                id: readTimer
                running: parent.isUnread
                interval: 10000
                onTriggered: {
                    viewModel.markItemAsRead(model.rawID);
                }
            }
        
            Item {
                anchors.fill: parent
        
                SvgImage {
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    anchors.verticalCenter: parent.verticalCenter 
        
                    source: getIconSource(type)
                }
        
                ColumnLayout {
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
                        Layout.fillWidth: true
                        text: model.message
                        font.pixelSize: 14
                        color: Style.content_main
                        elide: Text.ElideMiddle
                    }
        
                    RowLayout {
                        Layout.topMargin: 10
                        SFText {
                            Layout.rightMargin: 5
                            text: dateCreated
                            font.pixelSize: 12
                            color: Style.content_main
                            opacity: 0.5
                        }

                        SFText {
                            text: "|"
                            font.pixelSize: 12
                            color: Style.content_main
                            opacity: 0.5
                        }

                        SFText {
                            Layout.leftMargin: 5
                            text: timeCreated
                            font.pixelSize: 12
                            color: Style.content_main
                            opacity: 0.5
                        }
                        Item {
                            Layout.fillWidth: true
                        }

                    }
                    
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        
            CustomToolButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 20
                anchors.rightMargin: 20
        
                icon.source: "qrc:/assets/icon-cancel-white.svg"
                onClicked: {
                    viewModel.removeItem(model.rawID);
                }
            }
        
            CustomButton {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.bottomMargin: 20
                anchors.rightMargin: 20
        
                height: 38
                palette.button: Style.background_second
                palette.buttonText : Style.content_main
                icon.source: getActionButtonIcon(type).source
                icon.height: getActionButtonIcon(type).height
                text: getActionButtonLabel(type)
        
                visible: getActionButtonLabel(type) != undefined

                onClicked: {
                    notificationsViewRoot.notifications[type].action(model.rawID);
                }
            }
        }
    }

    function getIconSource(notificationType) {
        return "qrc:/assets/icon-notifications-" + notificationType;
    }

    property var icons: ({
        updateIcon: { source: 'qrc:/assets/icon-repeat-white.svg', height: 16},
        detailsIcon: { source: 'qrc:/assets/icon-details.svg', height: 12}
    })

    property var labels: ({
        //% "update now"
        updateLabel:    qsTrId("notifications-update-now"),
        //% "activate"
        activateLabel:  qsTrId("notifications-activate"),
        //% "details"
        detailsLabel:   qsTrId("notifications-details")
    })

    property var notifications: ({
        update: {
            label:      labels.updateLabel,
            icon:       icons.updateIcon,
            action:     updateClient
        },
        expired: {
            label:      labels.activateLabel,
            icon:       icons.updateIcon,
            action:     noAction
        },
        received: {
            label:      labels.detailsLabel,
            icon:       icons.detailsIcon,
            action:     navigateToTransaction
        },
        sent: {
            label:      labels.detailsLabel,
            icon:       icons.detailsIcon,
            action:     navigateToTransaction
        },
        failed: {
            label:      labels.detailsLabel,
            icon:       icons.detailsIcon,
            action:     navigateToTransaction
        }
    })

    function updateClient(id) {
        Utils.navigateToDownloads();
    }

    function navigateToTransaction(id) {
        var txID = viewModel.getItemTxID(id);
        if (txID.length > 0) {
            main.openTransactionDetails(txID);
        }
    }

    function noAction(id) {
        console.log("not implemented");
    }

    function getActionButtonLabel(notificationType) {
        return notificationsViewRoot.notifications[notificationType].label
    }

    function getActionButtonIcon(notificationType) {
        return notificationsViewRoot.notifications[notificationType].icon || ''
    }
} // Item
