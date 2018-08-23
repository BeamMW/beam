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

		icon.color: Style.white
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
			color: Style.white
			opacity: enabled ? 1.0 : 0.3
		}



        background: Rectangle {
            implicitWidth: 200
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color: control.highlighted ? Style.bright_sky_blue : "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 40
        color: Style.dark_slate_blue
		//opacity: 0.1
        radius: 10
    }
}
