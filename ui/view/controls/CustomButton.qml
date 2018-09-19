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

    font { 
        family: "SF Pro Display"
        pixelSize: 14
        styleName: "Bold"
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
        
        color: control.enabled ? control.palette.buttonText : Style.disable_text_color
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
    }

	DropShadow {
		anchors.fill: rect
		radius: 7
		samples: 9
		color: "white"
		source: rect
        // TODO (roman.strilets) maybe should using control.focus property
        // look at https://doc.qt.io/qt-5.9/qml-qtquick-controls2-control.html#visualFocus-prop
		visible: /*control.visualFocus*/control.activeFocus || control.hovered
	}
}