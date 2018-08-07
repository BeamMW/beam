import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."
Menu {
    topPadding: 20
    bottomPadding: 20

    delegate: MenuItem {
        id: menuItem
        implicitWidth: 200
        implicitHeight: 40

        contentItem: Text {
            text: menuItem.text
            font: menuItem.font
            opacity: enabled ? 1.0 : 0.3
            color: Style.white
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitWidth: 200
            implicitHeight: 40
            opacity: enabled ? 1 : 0.3
            color: menuItem.highlighted ? Style.bright_sky_blue : "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 40
        color: Styles.white
		opacity: 0.1
        radius: 10
    }
}
