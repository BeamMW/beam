import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtGraphicalEffects 1.0

ToolButton {
    id: control

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    baselineOffset: contentItem.y + contentItem.baselineOffset

	palette.buttonText: Style.content_main
	palette.highlight: Style.content_main
	palette.button: "transparent"
    padding: 8
    spacing: 8

    icon.width: 16
    icon.height: 16
    icon.color: visualFocus ? control.palette.highlight : control.palette.buttonText

    contentItem: IconLabel {
		id: icon
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        color: control.visualFocus ? control.palette.highlight : control.palette.buttonText
    }

    background: Rectangle {
		id: rect
        implicitWidth: 32
        implicitHeight: 32
		radius:16

        opacity: control.down ? 1.0 : 0.5
        color: "transparent"// control.down || control.checked || control.highlighted || control.hovered ? control.palette.mid : control.palette.button
    }

	DropShadow {
		anchors.fill: icon
		radius: 7
		samples: 9
		color: "white"
		source: icon
		visible: control.visualFocus || control.hovered
	}
}
