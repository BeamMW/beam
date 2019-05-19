import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "."

Item {
	property alias text: itemText.text
    property alias elide: itemText.elide
    anchors.fill: parent

    SFText {
        id: itemText
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        font.pixelSize: 14
        color: Style.content_main
    }

    clip:true
}
