import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import "."

Dialog {
    id: control
    property alias text: messageText.text
    property alias okButton: okButton
    property alias okButtonEnable: okButton.enabled
    property alias okButtonText: okButton.text
    property alias okButtonIconSource: okButton.icon.source
    property alias okButtonVisible: okButton.visible
    property alias okButtonColor: okButton.palette.button
    property alias okButtonAllLowercase: okButton.allLowercase
    property alias cancelButton: cancelButton
    property alias cancelButtonEnable: cancelButton.enabled
    property alias cancelButtonText: cancelButton.text
    property alias cancelButtonIconSource: cancelButton.icon.source
    property alias cancelButtonVisible: cancelButton.visible
    property alias cancelButtonColor: cancelButton.palette.button
    property alias cancelButtonAllLowercase: cancelButton.allLowercase

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
        color: Style.background_popup
        anchors.fill: parent
    }

    header: SFText {
        text: control.title
        topPadding: 30
        visible: control.title.length > 0
        horizontalAlignment : Text.AlignHCenter
        font.pixelSize: 18
        font.styleName: "Bold"; font.weight: Font.Bold
        color: Style.content_main
    }

    SFText {
        leftPadding: 20
        rightPadding: 20
        bottomPadding: 20
        topPadding: control.title.length > 0 ? 10 : 30
        id: messageText
        font.pixelSize: 14
        color: Style.content_main
        wrapMode: Text.Wrap
        horizontalAlignment : Text.AlignHCenter
        anchors.fill: parent
    }

    footer: Control {
        
        background: Rectangle {
            radius: 10
            color: Style.background_popup
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
                    //% "Cancel"
                    text: qsTrId("general-cancel")
                    onClicked: { 
                        rejected();
                        close();
                    }
                }

                CustomButton {
                    id: okButton
                    palette.button: Style.active
                    //% "Delete"
                    text: qsTrId("general-delete")
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
