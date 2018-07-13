import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {
	id: root

	width: 170
	height: 60

	radius: 30

	property string label

	signal clicked()

	gradient: Gradient {
        GradientStop {
            position: 0
            color: "#e173f7"
        }

        GradientStop {
            position: 1
            color: "#c03fec"
        }
    }

    SFText {
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter

        font.pixelSize: 18
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