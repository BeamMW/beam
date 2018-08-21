import QtQuick 2.3
import QtQuick.Controls 2.3
import "."

Dialog {
	id: control
    property alias text: messageText.text
    property alias okButtonText: okButton.text

	modal: true
	width: 520
	height: 200
	x: (parent.width - width) / 2
	y: (parent.height - height) / 2
	visible: false
		
	background: Rectangle {
		radius: 10
        color: Style.dark_slate_blue
        anchors.fill: parent            
    }

    contentItem: SFText {
        anchors.centerIn: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        width: parent.width
        id: messageText
        font.pixelSize: 14
        color: Style.white
        wrapMode: Text.WordWrap
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignHCenter
        spacing: 30

        background: Rectangle {
			radius: 10
            color: Style.dark_slate_blue
            anchors.fill: parent            
        }
            
		CustomButton {
            id: okButton
			palette.button: Style.bright_teal
			height: 38
            width: 166
			text: qsTr("delete")
			palette.buttonText: Style.marine
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
		}

        CustomButton {
            id: cancelButton
			palette.buttonText: Style.white
            height: 38
			width: 122
            focus: true
			text: qsTr("cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
		}
    }

    onOpened: {
        cancelButton.forceActiveFocus(Qt.TabFocusReason);
    }
}