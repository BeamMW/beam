import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

Dialog {
    onVisibleChanged: {
        if (!this.visible) {
            this.destroy();
        }
    }

    id: newAddressFailedDialog
    modal: true
    width: 400
    height: 160

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    visible: false

    background: Rectangle {
        radius: 10
        color: Style.background_second
        anchors.fill: parent
    }

    contentItem: Column {
        anchors.fill: parent
        anchors.margins: 30

        spacing: 40

        SFText {
            width: parent.width
            // text: qsTr("You cannot generate new address. Your wallet doesn't have a master key.")
            //% "You cannot generate new address. Your wallet doesn't have a master key."
            text: qsTrId("can-not-generate-new-address-message")
            color: Style.content_main
            font.pixelSize: 14
            font.styleName: "Bold"; font.weight: Font.Bold
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            // text: qsTr("ok")
            //% "Ok"
            text: qsTrId("general-ok")
            anchors.horizontalCenter: parent.horizontalCenter
            icon.source: "qrc:/assets/icon-done.svg"
            onClicked: newAddressFailedDialog.close()
        }
    }
}