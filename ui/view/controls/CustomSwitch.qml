import QtQuick 2.11
import QtQuick.Templates 2.4 as T
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import "."

T.Switch {
    id: control

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                             Math.max(contentItem.implicitHeight,
                                      indicator ? indicator.implicitHeight : 0) + topPadding + bottomPadding)
    baselineOffset: contentItem.y + contentItem.baselineOffset

    spacing: 12

    palette.text: Style.content_main

    contentItem: SFText {
        rightPadding: control.indicator.width + control.spacing
        text: control.text
        font.pixelSize: control.font.pixelSize
        font.styleName: control.font.styleName
        font.weight: control.font.weight
        color: control.palette.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Rectangle {
        implicitWidth: 30
        implicitHeight: 20
        x: control.width - width - control.rightPadding
        y: parent.height / 2 - height / 2
        radius: 10
        color: "transparent"
        border.color: Style.content_secondary

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 16
            height: 16
            radius: 10
            color: control.checked && control.enabled ? Style.active : Style.content_secondary
        }
    }
}