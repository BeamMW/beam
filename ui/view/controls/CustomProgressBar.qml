
import QtQuick 2.11
import QtQuick.Templates 2.4 as T
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtGraphicalEffects 1.0
import "."

T.ProgressBar {
    id: control

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                             contentItem.implicitHeight + topPadding + bottomPadding)

    property alias backgroundImplicitWidth: background_id.implicitWidth
    property alias contentItemImplicitWidth: content_id.implicitWidth

    contentItem: 
    Item {
        id: content_id
        implicitWidth: 200
        implicitHeight: 4
        Rectangle {
            radius: 10
            scale: control.mirrored ? -1 : 1
            width: control.visualPosition * parent.width
            height: parent.height
            color: Style.active
        }
    }

    background: Rectangle {
        id: background_id
        implicitWidth: 400
        implicitHeight: 4
        y: (control.height - height) / 2
        height: 4
        radius: 10
        border.color: Style.background_second
        color: "transparent"
    }

    DropShadow {
        anchors.fill: contentItem
        radius: 5
        samples: 9
        color: Style.active
        source: contentItem
    }
}
