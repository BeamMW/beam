import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls 2.2
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
        text: qsTr("Address book")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        Rectangle {
            color: "red"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        Rectangle {
            color: "green"
            anchors.fill: parent

        }

        TableView {
            id: peersView
            anchors.fill: parent

            frameVisible: false
            selectionMode: SelectionMode.SingleSelection
            backgroundVisible: false
            model: addressBookViewModel.peers

            TableViewColumn {
                role: "walletID"
                title: qsTr("Address ID")
                //width: 200*parent.width/1310

                movable: false
            }

            TableViewColumn {
                role: "name"
                title: qsTr("Name")
                //width: 200*parent.width/1310

                movable: false
            }

            TableViewColumn {
                role: "category"
                title: qsTr("Category")
                //width: 200*parent.width/1310
                movable: false
            }

            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                SFText {
                    anchors.verticalCenter: parent.verticalCenter

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

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: {

                    }
                }
            }

            itemDelegate: Item {

                anchors.fill: parent

                SFText {
                    anchors.verticalCenter: parent.verticalCenter

                    font.pixelSize: 12
                    color: Style.white

                    font.weight: Font.Normal

                    text: styleData.value
                }

                clip:true
            }
        }
    }
}
