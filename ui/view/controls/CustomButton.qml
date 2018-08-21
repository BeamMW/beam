import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.0
import QtQuick.Controls.impl 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Button {
    id: control
    
    palette.button: Style.separator_color
    property alias textOpacity: rect.opacity

    FontLoader { id: sf_pro_display; source: "qrc:///assets/fonts/SF-Pro-Display-Regular.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Bold.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Thin.otf"; }

    font {
        family: sf_pro_display.name
        pixelSize: 14
        weight: Font.Bold
    }

    width: 122
    height: 38
    
    activeFocusOnTab: true

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
        
        color: control.palette.buttonText
    }
    
    Keys.onPressed: {
        if (event.key == Qt.Key_Return || event.key == Qt.Key_Enter) control.clicked();
    }

    background: Rectangle {
        id: rect
        radius: 50
        color: control.enabled ? control.palette.button : "slategrey"
        
        width: control.width
        height: control.height

        MouseArea{
		    anchors.fill: parent
		    cursorShape: Qt.PointingHandCursor
            onClicked: control.clicked()
	    }
    }

	DropShadow {
		anchors.fill: rect
		radius: 7
		samples: 9
		color: "white"
		source: rect
		visible: control.visualFocus
	}
}