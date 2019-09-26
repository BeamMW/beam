import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import Beam.Wallet 1.0
import "."

Item {
	property alias text: itemText.text
    property alias elide: itemText.elide
    property alias color: itemText.color
    property alias fontWeight: itemText.font.weight
    property alias copyMenuEnabled: itemText.copyMenuEnabled
    signal copyText
    anchors.fill: parent

    SFLabel {
        id: itemText
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        font.pixelSize: 14
        color: Style.content_main
        copyMenuEnabled: true
        onCopyText: parent.copyText()
    }

    onCopyText: BeamGlobals.copyToClipboard(itemText.text)
}
