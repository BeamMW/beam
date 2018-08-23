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
}