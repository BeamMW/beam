import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"

ColumnLayout {
	anchors.fill: parent

	SFText {
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        font.pixelSize: 36
        color: Style.white
        text: qsTr("UTXO")
    }

	TableView {
		Layout.fillWidth: true
		Layout.fillHeight: true
		frameVisible: false
        selectionMode: SelectionMode.NoSelection
        backgroundVisible: false
		model: walletViewModel.allUtxos

		TableViewColumn {
			role: "amount"
			title: qsTr("Amount")
			width: 100
			movable: false
		}

		TableViewColumn {
			role: "height"
			title: qsTr("Height")
			width: 100
			movable: false
		}

		TableViewColumn {
			role: "maturity"
			title: qsTr("Maturity")
			width: 100
			movable: false
		}

		TableViewColumn {
			role: "status"
			title: qsTr("Status")
			width: 100
			movable: false
		}

		TableViewColumn {
			role: "type"
			title: qsTr("Type")
			width: 100
			movable: false
		}

		headerDelegate: Rectangle {
            height: 46

            color: Style.dark_slate_blue

            Text {
                anchors.verticalCenter: parent.verticalCenter
				anchors.left: parent.left
                anchors.leftMargin: 20
                font.pixelSize: 12
                color: Style.bluey_grey

                text: styleData.value
            }
        }

		rowDelegate: Item {

            height: 69

            anchors.left: parent.left
            anchors.right: parent.right

            Rectangle {
                anchors.fill: parent

                color: Style.light_navy
                visible: styleData.alternate
            }
        }

		itemDelegate: TableItem {
            text: styleData.value
        }
	}
}
