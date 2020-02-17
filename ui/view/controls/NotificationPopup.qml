import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import QtGraphicalEffects 1.0
import "."

Popup {
    id: popup

    property alias title: title.text
    property alias message: contentText.text
    property alias acceptButtonText: acceptButton.text

    width: 295
    height: 198
    modal: false
    parent: Overlay.overlay
    x: parent.width - width - 20        // 20 units from edge
    y: parent.height - height - 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    // padding: 0.0

    topPadding: 20
    bottomPadding: 20
    leftPadding: 20

    background: Rectangle {
        id: rect
        radius: 10
        // opacity: 0.1
        color: Style.background_popup
        anchors.fill: parent

        // TODO blur
        // ShaderEffectSource {
        //     id: blurSource
        //     sourceItem: rect
        //     anchors.fill: rect
        //     sourceRect: Qt.rect(popup.x,popup.y,popup.width,popup.height)
        //     recursive: true
        // }
        // FastBlur {
        //     id: blur
        //     anchors.fill: parent
        //     source: parent
        //     radius: 32
        // }
    }

    contentItem: Item {
        CustomToolButton {
            x: 235
            y: -10
            icon.source: "qrc:/assets/icon-cancel-white.svg"
            onClicked: { console.log("close popup click") }
        }

        Column {
            SFText {
                id: title
                width: 215
                leftPadding: 10
                visible: text.length > 0;
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight: Font.Bold
                color: Style.content_main
                horizontalAlignment : Text.AlignLeft
                wrapMode: Text.Wrap
            }

            
            SFText {
                id: contentText
                width: 235
                topPadding: 10
                leftPadding: 10
                
                font.pixelSize: 14
                color: Style.content_main
                horizontalAlignment : Text.AlignLeft
                wrapMode: Text.Wrap
            }
        }
        
        CustomButton {
            id: acceptButton
            x: 0
            y: 120
            // width: 165
            height: 38
            palette.button: Style.background_second
            palette.buttonText : Style.content_main
            icon.source: "qrc:/assets/icon-repeat-white.svg"
            onClicked: {
                console.log("update click")
            }
        }
    }
}
