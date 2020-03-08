import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

ConfirmationDialog {
    property string externalUrl
    property var onOkClicked: function () {}
    property var onCancelClicked: function () {}
    //% "allow and open"
    okButtonText: qsTrId("open-external-open")
    okButtonIconSource: "qrc:/assets/icon-external-link-black.svg"
    cancelButtonVisible: true
    cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
    width: 460
    leftPadding: 50
    rightPadding: 50
    
    //% "Allow access to Beam website"
    title: qsTrId("open-external-beam-site-title")

/*% "Beam Wallet app requires permission to open external link to Beam web-site in the browser to download updated version.
This action will expose your IP to the web server.
To avoid it, choose -Cancel-. You can change your choice in app setting anytime."*/
    text: qsTrId("update-from-external-message")

    onAccepted: {
        onOkClicked();
        Qt.openUrlExternally(externalUrl);
    }

    onRejected: {
        onCancelClicked();
    }
}