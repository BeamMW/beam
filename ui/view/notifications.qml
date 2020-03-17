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
        spacing: 0 // we emulate spacings with margins to avoid Flickable drag effect
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

        ScrollBar.vertical: ScrollBar {}

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
            height: 121+10
        
            property bool isUnread: model.state == "unread" 

            Rectangle {
                id: itemRect
                anchors.bottomMargin: 10
                radius: 10
                anchors.fill: parent
                color: (parent.isUnread) ? Style.active : Style.background_second
                opacity: (parent.isUnread) ? 0.1 : 1.0
            }

            MouseArea { // avoid Flickable drag effect
                anchors.fill: parent
                onPressed : {
                    notificationList.interactive = false;
                }
                onReleased : {
                    notificationList.interactive = true;
                }
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
                anchors.fill: itemRect
        
                SvgImage {
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    anchors.verticalCenter: parent.verticalCenter 
        
                    source: getIconSource(type)
                }
        
                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 20
                    anchors.bottomMargin: 20
                    anchors.leftMargin: 100
                    anchors.rightMargin: 150
        
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

                    Item {
                        Layout.fillHeight: true
                    }
        
                    RowLayout {
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
                }
            }
        
            CustomToolButton {
                anchors.top: itemRect.top
                anchors.right: itemRect.right
                anchors.topMargin: 12
                anchors.rightMargin: 12
                padding: 0
        
                icon.source: "qrc:/assets/icon-cancel-white.svg"
                onClicked: {
                    viewModel.removeItem(model.rawID);
                }
                onPressed : { // avoid Flickable drag effect
                    notificationList.interactive = false;
                }
                onReleased : {
                    notificationList.interactive = true;
                }
            }
        
            CustomButton {
                anchors.bottom: itemRect.bottom
                anchors.right: itemRect.right
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
                onPressed : { // avoid Flickable drag effect
                    notificationList.interactive = false;
                }
                onReleased : {
                    notificationList.interactive = true;
                }
            }
        }
    }

    function getIconSource(notificationType) {
        return notificationsViewRoot.notifications[notificationType].icon || ''
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
            actionIcon: icons.updateIcon,
            action:     updateClient,
            icon:       "qrc:/assets/icon-notifications-update.svg"
        },
        expired: {
            label:      labels.activateLabel,
            actionIcon: icons.updateIcon,
            action:     noAction,
            icon:       "qrc:/assets/icon-notifications-expired.svg"
        },
        received: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToTransaction,
            icon:       "qrc:/assets/icon-notifications-received.svg"
        },
        sent: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToTransaction,
            icon:       "qrc:/assets/icon-notifications-sent.svg"
        },
        failedToSend: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToTransaction,
            icon:       "qrc:/assets/icon-notifications-sending-failed.svg"
        },
        failedToReceive: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToTransaction,
            icon:       "qrc:/assets/icon-notifications-receiving-failed.svg"
        },
        swapFailed: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToSwapTransaction,
            icon:       "qrc:/assets/icon-notifications-swap-failed.svg"
        },
        swapExpired: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToSwapTransaction,
            icon:       "qrc:/assets/icon-notifications-swap-expired.svg"
        },
        swapCompleted: {
            label:      labels.detailsLabel,
            actionIcon: icons.detailsIcon,
            action:     navigateToSwapTransaction,
            icon:       "qrc:/assets/icon-notifications-swap-completed.svg"
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

    function navigateToSwapTransaction(id) {
        var txID = viewModel.getItemTxID(id);
        if (txID.length > 0) {
            main.openSwapTransactionDetails(txID);
        }
    }

    function noAction(id) {
        console.log("not implemented");
    }

    function getActionButtonLabel(notificationType) {
        return notificationsViewRoot.notifications[notificationType].label
    }

    function getActionButtonIcon(notificationType) {
        return notificationsViewRoot.notifications[notificationType].actionIcon || ''
    }
} // Item
