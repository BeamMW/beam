import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import Beam.Wallet 1.0
import "."

T.TextField {
    id: control
    signal textPasted()

    function getMousePos() {
        return {x: mouseArea.mouseX, y: mouseArea.mouseY}
    }

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            placeholderText ? placeholder.implicitWidth + leftPadding + rightPadding : 0)
                            || contentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             background ? background.implicitHeight : 0,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    font { 
        family: "SF Pro Display"
        styleName: "Regular"
    }

    padding: 6
    leftPadding: 0

    color: control.palette.text
    selectionColor: control.palette.highlight
    selectedTextColor: control.palette.highlightedText
    verticalAlignment: TextInput.AlignVCenter

    property bool  focusablePlaceholder: false
    property alias backgroundColor : backgroundRect.color
    property alias underlineVisible : backgroundRect.visible
    backgroundColor: Style.content_main

	selectByMouse: true
	
    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)

        text: control.placeholderText
        font: control.font
        opacity: 0.5
        color: control.color
        verticalAlignment: control.verticalAlignment
        horizontalAlignment: control.horizontalAlignment
        visible:  (focusablePlaceholder || !control.activeFocus) && !control.length && !control.preeditText
        elide: Text.ElideRight
        wrapMode: control.wrapMode
    }

    background: Rectangle {
	    id: backgroundRect
        y: control.height - height - control.bottomPadding + 4
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.activeFocus || control.hovered ? 1 : 1
		opacity: (control.activeFocus || control.hovered)? 0.3 : 0.1
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        hoverEnabled: true
        id: mouseArea

        onClicked: {
            var selectStart = control.selectionStart
            var selectEnd = control.selectionEnd
            contextMenu.x = mouseX
            contextMenu.y = mouseY
            contextMenu.open()
            if (cursorPosition == selectEnd) control.select(selectStart, selectEnd)
            else control.select(selectEnd, selectStart)
        }
    }

    ContextMenu {
        id: contextMenu
        modal: true
        dim: false
        Action {
            //% "Copy"
            text: qsTrId("general-copy")
            icon.source: "qrc:/assets/icon-copy.svg"
            enabled: control.enabled && (control.echoMode === TextInput.Normal)
            onTriggered: {
                if (control.selectedText.length > 0) {
                    control.copy();
                }
                else {
                    BeamGlobals.copyToClipboard(control.text)
                }
            }
        }
        Action {
            //% "Paste"
            text: qsTrId("general-paste")
            icon.source: "qrc:/assets/icon-edit.svg"
            enabled: control.canPaste
            onTriggered: {
                control.paste()
                control.textPasted()
            }
        }

        property bool inputFocus: false

        onAboutToShow: {
            // save input state before menu
            inputFocus = control.focus
            // we always force focus on menu
            control.forceActiveFocus()
        }

        onClosed: {
            // restore input state after menu
            if (inputFocus) {
                var selectStart = control.selectionStart
                var selectEnd   = control.selectionEnd
                control.forceActiveFocus()
                if (cursorPosition == selectEnd) control.select(selectStart, selectEnd)
                else control.select(selectEnd, selectStart)
            } else {
                backgroundRect.forceActiveFocus()
            }
        }
    }

    Keys.onShortcutOverride: event.accepted = event.matches(StandardKey.Paste)
    Keys.onPressed: {
        if (event.matches(StandardKey.Paste)) {
            event.accepted = true
            control.paste()
            control.textPasted()
        }
    }
}
