import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

ConfirmationDialog {
    property string externalUrl
    property string version
    property var onOkClicked: function () {}
    property var onCancelClicked: function () {}

    //% "open web-site"
    okButtonText:           qsTrId("update-open-url-button")
    okButtonIconSource:     "qrc:/assets/icon-external-link-black.svg"
    //% "cancel"
    cancelButtonText:       qsTrId("update-cancel-button")
    cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"

    width: 460
    height: 297

    contentItem: Item {
        Column {
            anchors.fill: parent
            
            SFText {
                width:          parent.width
                topPadding:     20
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight:    Font.Bold
                color:          Style.content_main
                horizontalAlignment: Text.AlignHCenter
                //% "New update available"
                text:           qsTrId("update-notification-title")
            }
            SFText {
                width:          parent.width
                topPadding:     15
                leftPadding:    20
                rightPadding:   20
                font.pixelSize: 14
                color:          Style.content_main
                wrapMode:       Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                //% "Update %1 is available to download. You can update later from Settings."
                text:           qsTrId("update-notification-message").arg(version)
            }
            SFText {
                width:          parent.width
                topPadding:     15
                leftPadding:    20
                rightPadding:   20
                font.pixelSize: 14
                color:          Style.content_main
                wrapMode:       Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                //% "Beam Wallet app requires permission to open external link to Beam web-site in the browser to download updated version."
                //% "This action will expose your IP to the web server. To avoid it, choose -Cancel-. You can change your choice in app setting anytime."
                text:           qsTrId("update-notification-text")
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
