import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "."

TableView {
	style: TableViewStyle {
		transientScrollBars: true
		handle: Rectangle {
            implicitWidth: 14
            implicitHeight: 16
			radius: 6
			anchors.fill: parent
			color: Style.white
			opacity: 0.1
		}
	}
    horizontalScrollBarPolicy: Qt.ScrollBarAlwaysOff

    headerDelegate: Rectangle {
        height: 46

        color: Style.dark_slate_blue

        SFText {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 20
            font.pixelSize: 14
            color: Style.bluey_grey

            text: styleData.value
        }
    }
}