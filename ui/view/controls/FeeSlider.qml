import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import QtGraphicalEffects 1.0

T.Slider {
    id: control

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                           (handle ? handle.implicitWidth : 0) + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                            (handle ? handle.implicitHeight : 0) + topPadding + bottomPadding) + 30

    padding: 0

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 20
        implicitHeight: 20
        radius: width / 2
        color: Style.bright_teal

        SFText {

            x: {
                if(control.value < control.stepSize) 0
                else if(control.value > control.to - control.stepSize) parent.width-width
                else (parent.width-width)/2
            }

            y: -20
            font.pixelSize: 14
            text: control.value.toFixed(6)
            color: Style.bright_teal
        }
    }

    DropShadow {
        anchors.fill: handle
        radius: 5
        samples: 9
        color: handle.color
        source: handle
        visible: control.visualFocus || control.hovered
    }

    background: Item {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 200 : 4
        implicitHeight: control.horizontal ? 4 : 200
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        scale: control.horizontal && control.mirrored ? -1 : 1

        Rectangle {
            anchors.fill: parent
            color: Style.white
            opacity: 0.1
            radius: 2
        }

        SFText {
            anchors.top: parent.bottom
            anchors.topMargin: 10
            font.pixelSize: 14
            color: Style.bluey_grey
            text: control.from.toFixed(6)
        }

        SFText {
            anchors.top: parent.bottom
            anchors.topMargin: 10
            anchors.right: parent.right
            font.pixelSize: 14
            color: Style.bluey_grey
            text: control.to.toFixed(6)
        }
    }
}
