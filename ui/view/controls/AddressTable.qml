import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls 2.3
import "."

CustomTableView {
    id: rootControl

    property int rowHeight: 69

    anchors.fill: parent
    frameVisible: false
    selectionMode: SelectionMode.NoSelection
    backgroundVisible: false    

    TableViewColumn {
        role: "name"
        title: qsTr("Name")
        width: 150 * parent.width / 800
        resizable: false
        movable: false
    }

    TableViewColumn {
        role: "address"
        title: qsTr("Address")
        width: 150 * parent.width / 800
        movable: false
        resizable: false
        delegate: Item {
            Item {
                width: parent.width
                height: rootControl.rowHeight
                clip:true

                SFLabel {
                    font.pixelSize: 14
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    elide: Text.ElideMiddle
                    anchors.verticalCenter: parent.verticalCenter
                    text: styleData.value
                    color: Style.white
                    copyMenuEnabled: true
                    onCopyText: viewModel.copyToClipboard(text)
                }
            }
        }
    }

    TableViewColumn {
        role: "category"
        title: qsTr("Category")
        width: 150 * parent.width / 800
        resizable: false
        movable: false
    }

    TableViewColumn {
        role: "expirationDate"
        title: qsTr("Expiration date")
        width: 150 * parent.width / 800
        resizable: false
        movable: false
    }

    TableViewColumn {
        role: "createDate"
        title: qsTr("Created")
        width: 150 * parent.width / 800
        resizable: false
        movable: false
    }

    TableViewColumn {
        //role: "status"
        title: ""
        width: 40
        movable: false
        resizable: false
        delegate: txActions
    }

    rowDelegate: Item {

        height: rootControl.rowHeight

        anchors.left: parent.left
        anchors.right: parent.right

        Rectangle {
            anchors.fill: parent

            color: styleData.selected ? Style.bright_sky_blue : Style.light_navy
            visible: styleData.selected ? true : styleData.alternate
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: {
                /*if (mouse.button === Qt.RightButton && styleData.row !== undefined)
                {
                    peerAddressContextMenu.index = styleData.row;
                    peerAddressContextMenu.popup();
                }*/
            }
        }
    }

    itemDelegate: TableItem {
        text: styleData.value
        elide: styleData.elideMode
    }

    Component {
        id: txActions
        Item {
            Item {
                width: parent.width
                height: rootControl.rowHeight

                Row{
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    CustomToolButton {
                        icon.source: "qrc:/assets/icon-actions.svg"
                        ToolTip.text: qsTr("Actions")
                        onClicked: {
                            /*txContextMenu.transaction = viewModel.transactions[styleData.row];
                            txContextMenu.popup();*/
                        }
                    }
                }
            }
        }
    }

    ContextMenu {
        id: txContextMenu
        modal: true
        dim: false
        //property TxObject transaction
        Action {
            text: qsTr("transactions list")
            icon.source: "qrc:/assets/icon-transactions.svg"
            onTriggered: {
                /*if (!!txContextMenu.transaction)
                {
                    viewModel.copyToClipboard(txContextMenu.transaction.user);
                }*/
            }
        }
        Action {
            text: qsTr("delete")
            icon.source: "qrc:/assets/icon-delete.svg"
            enabled: !!txContextMenu.transaction && txContextMenu.transaction.canDelete
            onTriggered: {
                /*deleteTransactionDialog.text = qsTr("The transaction will be deleted. This operation can not be undone");
                deleteTransactionDialog.open();*/
            }
        }
        /*Connections {
            target: deleteTransactionDialog
            onAccepted: {
                viewModel.deleteTx(txContextMenu.transaction);
            }
        }*/
    }
}