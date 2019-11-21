import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.0
import QtQuick.Controls.impl 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Button {
    id: control
    
    palette.button: Style.separator
    palette.buttonText: Style.content_main
    property alias textOpacity: rect.opacity
    property alias shadowSamples: drop_shadow.samples
    property alias shadowRadius: drop_shadow.radius
    property bool allLowercase: !text.startsWith("I ")

    font { 
        family: "SF Pro Display"
        pixelSize: 14
        styleName: "Bold"; weight: Font.Bold
        capitalization: allLowercase ? Font.AllLowercase : Font.MixedCase
    }

//    width: 122
    height: 38
    leftPadding: 30
    rightPadding: 30
    
    activeFocusOnTab: true

    spacing: 15
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
    
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        
        color: control.enabled ? control.palette.buttonText : Style.content_disabled
    }
    
    Keys.onPressed: {
        if (event.key == Qt.Key_Return || event.key == Qt.Key_Enter) control.clicked();
    }

    background: Rectangle {
        id: rect
        radius: 50
        color: control.enabled ? control.palette.button : Style.content_disabled
        opacity: control.enabled ? 1.0 : 0.6
        
        width: control.width
        height: control.height
    }

    DropShadow {
        id: drop_shadow
        anchors.fill: rect
        radius: 7
        samples: 9
        color: Style.content_main
        source: rect
        visible: control.visualFocus || control.hovered
    }
}
