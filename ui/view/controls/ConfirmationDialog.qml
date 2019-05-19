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
    property alias okButtonEnable: okButton.enabled
    property alias cancelVisible: cancelButton.visible
    property alias cancelEnable: cancelButton.enabled
    property alias cancelButtonIconSource: cancelButton.icon.source
    property alias okButton: okButton
    property alias cancelButton: cancelButton
    function confirmationHandler() {
        accepted();
        close();
    }

    function openHandler() {
        cancelButton.forceActiveFocus(Qt.TabFocusReason);
    } 

    modal: true

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    visible: false
        
    background: Rectangle {
        radius: 10
        color: Style.background_second
        anchors.fill: parent
    }

    SFText {
        id: messageText
        anchors.fill: parent
        padding: 20
        font.pixelSize: 14
        color: Style.content_main
        wrapMode: Text.Wrap
        horizontalAlignment : Text.AlignHCenter
    }

    footer: Control {
        
        background: Rectangle {
            radius: 10
            color: Style.background_second
            anchors.fill: parent
        }          

        contentItem: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            Row {
                spacing: 20
                height: 40
                leftPadding: 30
                rightPadding: 30
                bottomPadding: 30
                CustomButton {
                    id: cancelButton
                    focus: true
                    //% "cancel"
                    text: qsTrId("confirmation-cancel-button")
                    onClicked: { 
                        rejected();
                        close();
                    }
                }

                CustomButton {
                    id: okButton
                    palette.button: Style.active
                    //% "delete"
                    text: qsTrId("confirmation-delete-button")
                    palette.buttonText: Style.content_opposite
                    onClicked: {
                        confirmationHandler();
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }

    onOpened: {
        openHandler();
    }
}