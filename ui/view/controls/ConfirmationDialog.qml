import QtQuick 2.11
import QtQuick.Controls 2.3
import "."

Dialog {
    id: control
    property alias text: messageText.text
    property alias okButtonText: okButton.text
    property alias okButtonIconSource: okButton.icon.source
    property alias cancelVisible : cancelButton.visible

    modal: true

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    visible: false
        
    background: Rectangle {
        radius: 10
        color: Style.dark_slate_blue
        anchors.fill: parent
    }

    contentItem: SFText {
        id: messageText
        padding: 30
        bottomPadding: 15
        font.pixelSize: 14
        color: Style.white
        wrapMode: Text.Wrap
        horizontalAlignment : Text.AlignHCenter
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignHCenter
        spacing: 30
        padding: 30
        topPadding: 15
        leftPadding: cancelButton.visible ? 0: 160
        background: Rectangle {
            radius: 10
            color: Style.dark_slate_blue
            anchors.fill: parent
        }
            
        CustomButton {
            id: okButton
            palette.button: Style.bright_teal
            text: qsTr("delete")
            palette.buttonText: Style.marine
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

        CustomButton {
            id: cancelButton
            focus: true
            text: qsTr("cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onOpened: {
        cancelButton.forceActiveFocus(Qt.TabFocusReason);
    }
}