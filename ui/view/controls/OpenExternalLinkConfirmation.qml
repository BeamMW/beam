import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

ConfirmationDialog {
    property string externalUrl
    property var onOkClicked: function () {}
    property var onCancelClicked: function () {}
    //% "Open"
    okButtonText: qsTrId("open-external-open")
    okButtonIconSource: "qrc:/assets/icon-external-link-black.svg"
    cancelButtonVisible: true
    cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
    width: 460
    height: 217

    contentItem: Item {
        Column {
            anchors.fill: parent
            
            SFText {
                width: parent.width
                topPadding: 20
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight: Font.Bold
                color: Style.content_main
                horizontalAlignment : Text.AlignHCenter
                //% "External link"
                text: qsTrId("open-external-title")
            }
            SFText {
                width: parent.width
                topPadding: 15
                leftPadding: 15
                rightPadding: 15
                font.pixelSize: 14
                color: Style.content_main
                wrapMode: Text.Wrap
                horizontalAlignment : Text.AlignHCenter
                //% "Beam Wallet app requires permission to open external link in the browser. This action will expose your IP to the web server. To avoid it, choose -Cancel-. You can change your choice in app setting anytime."
                text: qsTrId("open-external-message")
            }
        }
    }

    onAccepted: {
        onOkClicked();
        Qt.openUrlExternally(externalUrl);
    }

    onRejected: {
        onCancelClicked();
    }
}