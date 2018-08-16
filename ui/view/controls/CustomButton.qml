import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.0
import QtQuick.Controls.impl 2.4
import "."

Button {
    id: control

    property alias color: rect.color
    property string textColor

    FontLoader { id: sf_pro_display; source: "qrc:///assets/fonts/SF-Pro-Display-Regular.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Bold.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Thin.otf"; }

    font {
        family: sf_pro_display.name
        pixelSize: 14
        weight: Font.Bold
    }

    spacing: 15
    icon.color: "transparent"
    icon.width: 10
    icon.height: 10
    
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        
        color: control.textColor
    }

    background: Rectangle {
        id: rect
	    radius: 50
	    color: Style.separator_color
	    width: control.width
	    height: control.height
    }
}