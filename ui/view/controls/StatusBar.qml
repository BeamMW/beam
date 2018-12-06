import QtQuick 2.11
import QtQuick.Controls 2.3
import QtGraphicalEffects 1.0
import Beam.Wallet 1.0
import "."

Item {
    id: rootControl
    x: 5
    y: 53

    StatusbarViewModel {
        id: model
    }

    property string status: {
        if (model.isisFailedStatus)
            qsTr("error")
        else if (model.isOfflineStatus)
            qsTr("offline")
        else if(model.isSyncInProgress)
            qsTr("updating")
        else
            qsTr("online")
    }

    state: "online"

    property int indicator_radius: 5
    property Item indicator: online_indicator
    property string error_msg: model.walletStatusErrorMsg

    function setIndicator(indicator) {
        if (indicator !== rootControl.indicator) {
            rootControl.indicator.visible = false;
            rootControl.indicator = indicator;
            rootControl.indicator.visible = true;
        }
    }

    Item {
        id: online_indicator
        anchors.top: parent.top
        anchors.left: parent.left
        width: childrenRect.width

        property color color: Style.bright_teal
        property int radius: rootControl.indicator_radius

        Rectangle {
            id: online_rect
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.topMargin: 2

            width: rootControl.indicator_radius * 2
            height: rootControl.indicator_radius * 2
            radius: rootControl.indicator_radius
            color: parent.color
        }

        DropShadow {
            anchors.fill: online_rect
            radius: 5
            samples: 9
            source: online_rect
            color: parent.color
        }
    }

    Item {
        id: offline_indicator
        anchors.top: parent.top
        anchors.left: parent.left
        width: childrenRect.width
        visible: false

        property color color: Style.bluey_grey
        property int radius: rootControl.indicator_radius


        Rectangle {
            id: offline_rect
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.topMargin: 2

            color: "transparent"
            width: parent.radius * 2
            height: parent.radius * 2
            radius: parent.radius
            border.color: parent.color
            border.width: 1
        }
    }

    Item {
        id: update_indicator
        anchors.top: parent.top
        anchors.left: parent.left
        visible: false

        property color color: Style.bright_teal
        property int circle_line_width: 2
        property int animation_duration: 2000

        width: 2 * rootControl.indicator_radius + circle_line_width
        height: 2 * rootControl.indicator_radius + circle_line_width

        Canvas {
            id: canvas_
            anchors.fill: parent
            onPaint: {
                var context = getContext("2d");
                context.arc(width/2, height/2, width/2 - parent.circle_line_width, 0, 1.6 * Math.PI);
                context.strokeStyle = parent.color;
                context.lineWidth = parent.circle_line_width;
                context.stroke();
            }
        }

        RotationAnimator {
            target: update_indicator
            from: 0
            to: 360
            duration: update_indicator.animation_duration
            running: update_indicator.visible
            loops: Animation.Infinite
        }
    }

    SFText {
        id: status_text
        anchors.top: parent.top
        anchors.left: parent.indicator.right
        anchors.leftMargin: 5
        anchors.topMargin: -3
        color: Style.bluey_grey
        font.pixelSize: 14
    }
    SFText {
        id: progressText
        anchors.top: parent.top
        anchors.left: status_text.right
        anchors.leftMargin: 5
        anchors.topMargin: -3
        color: Style.bluey_grey
        font.pixelSize: 14
        text: "(" + model.nodeSyncProgress + "%)"
        visible: model.nodeSyncProgress > 0 && update_indicator.visible
    }

    CustomProgressBar {
        id: progress_bar
        anchors.top: update_indicator.bottom
        anchors.left: update_indicator.left
        anchors.topMargin: 6
        backgroundImplicitWidth: 200
        contentItemImplicitWidth: 200

        visible: model.nodeSyncProgress > 0 && update_indicator.visible
        value: model.nodeSyncProgress / 100
    }

    states: [
        State {
            name: "online"
            when: (rootControl.status === "online")
            PropertyChanges {target: status_text; text: qsTr("online") + model.branchName}
            StateChangeScript {
                name: "onlineScript"
                script: {
                    rootControl.setIndicator(online_indicator);
                }
            }
        },
        State {
            name: "offline"
            when: (rootControl.status === "offline")
            PropertyChanges {target: status_text; text: qsTr("offline") + model.branchName}
            StateChangeScript {
                name: "offlineScript"
                script: {
                    rootControl.setIndicator(offline_indicator);
                }
            }
        },
        State {
            name: "updating"
            when: (rootControl.status === "updating")
            PropertyChanges {target: status_text; text: qsTr("updating...") + model.branchName}
            StateChangeScript {
                name: "updatingScript"
                script: {
                    rootControl.setIndicator(update_indicator);
                }
            }
        },
        State {
            name: "error"
            when: (rootControl.status === "error")
            PropertyChanges {target: status_text; text: rootControl.error_msg + model.branchName}
            StateChangeScript {
                name: "errorScript"
                script: {
                    online_indicator.color = "red";
                    rootControl.setIndicator(online_indicator);
                }
            }
        }
    ]
    transitions: [
        Transition {
            from: "online"
            to: "updating"
            SequentialAnimation {
                PauseAnimation { duration: 1000 }
                ScriptAction { scriptName: "updatingScript" }
            }
        }
    ]
}
