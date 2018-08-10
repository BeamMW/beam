import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Item {
	id: root

    width: text.width + 60
    height: text.height + 24

    property string label

    signal clicked()

    Rectangle {

        anchors.fill: parent

	    radius: 50

        color: Style.white
        opacity: 0.1
    }

    SFText {
        id: text
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter

        font.pixelSize: 12
        font.weight: Font.Bold

        color: Style.white

        text: root.label
    }

    MouseArea{
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked();
    }
}