import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import QtGraphicalEffects 1.0
import "."


T.RadioButton {
    id: control
	palette.windowText: Style.white
	palette.text: Style.bright_teal

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(background ? background.implicitHeight : 0,
                             Math.max(contentItem.implicitHeight,
                                      indicator ? indicator.implicitHeight : 0) + topPadding + bottomPadding)
    baselineOffset: contentItem.y + contentItem.baselineOffset

    padding: 6
    spacing: 6

    // keep in sync with RadioDelegate.qml (shared RadioIndicator.qml was removed for performance reasons)
    indicator: Rectangle {
	    id: indicatorRect
        implicitWidth: 28
        implicitHeight: 28

        x: text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        radius: width / 2
        color: "transparent"//control.down ? control.palette.light : control.palette.base
        border.width: 1 //control.visualFocus ? 2 : 1
        border.color: Style.bluey_grey //control.visualFocus ? control.palette.highlight : control.palette.mid

        Rectangle {
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: 20
            height: 20
            radius: width / 2
            color: control.palette.text
            visible: control.checked
        }
    }

	DropShadow {
		anchors.fill: indicatorRect
		radius: 7
		samples: 9
		color: "white"
		source: indicatorRect
        // TODO (roman.strilets) maybe should using control.focus property
        // look at https://doc.qt.io/qt-5.9/qml-qtquick-controls2-control.html#visualFocus-prop
		visible: /*control.visualFocus*/control.activeFocus
	}

    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: control.palette.windowText
        opacity: control.enabled ? 1.0 : 0.3
    }
}