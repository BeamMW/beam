import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Menu {
	id: menu
    topPadding: 20
    bottomPadding: 20

    delegate: MenuItem {
        id: control
        implicitWidth: 200
        implicitHeight: 40

		icon.color: Style.content_main
		icon.width: 12
		icon.height: 12
		font.pixelSize: 14
		
		spacing: 14

		contentItem: IconLabel {

			leftPadding: 20
			rightPadding: 20
			

			spacing: control.spacing
			mirrored: control.mirrored
			display: control.display
			alignment: Qt.AlignLeft

			icon: control.icon
			text: control.text
			font: control.font
			color: Style.content_main
			opacity: enabled ? 1.0 : 0.3
		}



        background: Rectangle {
            implicitWidth: 200
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color: control.hovered ? Style.accent_incoming : "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 40
        border.width: 1
        border.color: Style.separator
        color: Style.background_second
		//opacity: 0.1
        radius: 10
    }
}
