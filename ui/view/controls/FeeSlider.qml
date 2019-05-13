import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import QtGraphicalEffects 1.0

T.Slider {
    id: control

    property int precision: 6

    property bool showTicks: false

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                           (handle ? handle.implicitWidth : 0) + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                            (handle ? handle.implicitHeight : 0) + topPadding + bottomPadding) + 40

    padding: 0

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 20
        implicitHeight: 20
        radius: width / 2
        color: control.enabled ? Style.active : Style.content_disabled

        SFText {

            x: {
                if(control.value < control.stepSize) 0
                else if(control.value > control.to - control.stepSize) parent.width-width
                else (parent.width-width)/2
            }

            y: -26
            font.pixelSize: 14
            text: control.value.toFixed(control.precision)
            color: control.enabled ? Style.active : Style.content_disabled
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

        Repeater {
            anchors.fill: parent
            Rectangle {
                y: (handle.width / 2) + 4
                x: (handle.width / 2) + ((control.availableWidth - handle.width) / control.to) * index
                width: 1
                height: 4
                color: Style.content_secondary
                visible: control.showTicks
            }
            model: control.to + 1
           
        }

        Item {
            width: parent.width
            anchors.top: parent.bottom
            anchors.topMargin: 14
            
            SFText {
                font.pixelSize: 14
                color: Style.content_secondary
                text: control.from.toFixed(control.precision)
            }

            SFText {
                anchors.right: parent.right
                font.pixelSize: 14
                color: Style.content_secondary
                text: control.to.toFixed(control.precision)
            }            
        }
    }
}
