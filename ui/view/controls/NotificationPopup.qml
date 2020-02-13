import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import "."

Popup {
    id: popup

    property string titleText
    property string messageText
    property string acceptButtonText

    width: 295
    height: 198
    modal: true
    parent: Overlay.overlay
    x: parent.width - width - 20
    y: parent.height - height - 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    topPadding: 20
    bottomPadding: 20
    leftPadding: 20

    background: Rectangle {
        radius: 10
        // opacity: 0.1
        color: Style.background_popup
        anchors.fill: parent
    }

    contentItem: ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Row {
            Layout.fillWidth: true
            SFText {
                id: title
                width: 215
                // topPadding: 10
                leftPadding: 10
                rightPadding: 20
                visible: text.length > 0;
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight: Font.Bold
                color: Style.content_main
                horizontalAlignment : Text.AlignLeft
                text: popup.titleText
                wrapMode: Text.Wrap
            }

            CustomToolButton {
                icon.source: "qrc:/assets/icon-cancel-white.svg"
                onClicked: { console.log("close popup click") }
            }
        }
        
        SFText {
            id: contentText
            Layout.fillWidth: true
            // topPadding: 10
            leftPadding: 10
            rightPadding: 30
            
            font.pixelSize: 14
            color: Style.content_main
            horizontalAlignment : Text.AlignLeft
            text: messageText
            wrapMode: Text.Wrap
        }

        CustomButton {
            id: acceptButton
            // topPadding: 20
            width: 165
            height: 38
            palette.button: Style.background_second
            palette.buttonText : Style.content_main
            icon.source: "qrc:/assets/icon-repeat-white.svg"
            text: acceptButtonText
            onClicked: {
                console.log("update click")
            }
        }
    }
}
