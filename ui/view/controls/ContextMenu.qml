import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import QtGraphicalEffects 1.0
import QtQuick.Window 2.11
import "."

T.Menu {
	id: control
    topPadding: 20
    bottomPadding: 20
    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 0)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                             contentItem ? contentItem.implicitHeight : 0) + topPadding + bottomPadding

    margins: 0
    overlap: 1

    delegate: MenuItem {
        id: itemControl
		icon.color: Style.content_main
		icon.width: 12
		icon.height: 12
		font.pixelSize: 14
        font.capitalization: Font.AllLowercase

		spacing: 14

		contentItem: IconLabel {
			leftPadding: 20
			rightPadding: 20

			spacing: itemControl.spacing
			mirrored: itemControl.mirrored
			display: itemControl.display
			alignment: Qt.AlignLeft

			icon: itemControl.icon
			text: itemControl.text
			font: itemControl.font
			color: Style.content_main
			opacity: enabled ? 1.0 : 0.3
		}

        background: Rectangle {
            implicitWidth: 130
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color: itemControl.hovered ? Style.accent_incoming : "transparent"
        }
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        implicitWidth: childrenRect.width
        model: control.contentModel
        interactive: Window.window ? contentHeight > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex

        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 40
        border.width: 1
        border.color: Style.separator
        color: Style.background_popup
        radius: 10
    }

    T.Overlay.modal: Rectangle {
        color: Color.transparent(control.palette.shadow, 0.5)
    }

    T.Overlay.modeless: Rectangle {
        color: Color.transparent(control.palette.shadow, 0.12)
    }
}
