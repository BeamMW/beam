import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import "."

Dialog {
    id: control
    property alias text: messageText.text
    property alias okButtonText: okButton.text
    property alias okButtonIconSource: okButton.icon.source
    property alias okButtonColor: okButton.palette.button
    property alias cancelVisible : cancelButton.visible
    property alias cancelButtonIconSource: cancelButton.icon.source

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

    footer: Control {
        
        background: Rectangle {
            radius: 10
            color: Style.dark_slate_blue
            anchors.fill: parent
        }          

        contentItem: GridLayout {
            id: test
            columns: 3
            Row {
                Layout.fillWidth: true
            }
            Row {
                spacing: 30
                height: 40
                padding: 30
                topPadding: 15
                CustomButton {
                    id: cancelButton
                    focus: true
                    text: qsTr("cancel")
                    onClicked: {                
                        rejected();
                        close();
                    }
                }

                CustomButton {
                    id: okButton
                    palette.button: Style.bright_teal
                    text: qsTr("delete")
                    palette.buttonText: Style.marine
                    onClicked: {
                        accepted();
                        close();
                    }
                }
            }
            Row {
                Layout.fillWidth: true
            }
        }
    }

    onOpened: {
        cancelButton.forceActiveFocus(Qt.TabFocusReason);
    }
}