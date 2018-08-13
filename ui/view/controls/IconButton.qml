import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {
	id: root
	radius: 50
	color: Style.separator_color
	width: 122
	height: 38

	property string label
	property string textColor
	property string iconName

	signal clicked()

	RowLayout {
		anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
		SvgImage {
			source: "qrc:///assets/" + root.iconName +".svg"
		}

		SFText {
			Layout.leftMargin: 15
			font.pixelSize: 14
			font.weight: Font.Bold

			color: textColor
			text: root.label
		}
	}

	MouseArea{
		anchors.fill: parent
		cursorShape: Qt.PointingHandCursor
		onClicked: {
			root.clicked()
		}
	}
}