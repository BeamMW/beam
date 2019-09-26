import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls 2.3
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils
import Beam.Wallet 1.0

CustomTableView {
    id: rootControl

    property int rowHeight: 56
    property int resizableWidth: parent.width - actions.width
    property double columnResizeRatio: resizableWidth / 750

    property var parentModel
    property bool isExpired: false
    property var editDialog
    property var showQRDialog
    anchors.fill: parent
    frameVisible: false
    selectionMode: SelectionMode.NoSelection
    backgroundVisible: false
    sortIndicatorVisible: true
    sortIndicatorColumn: 4
    sortIndicatorOrder: Qt.DescendingOrder

    onSortIndicatorColumnChanged: {
        if (sortIndicatorColumn != 3 &&
            sortIndicatorColumn != 4) {
            sortIndicatorOrder = Qt.AscendingOrder;
        } else {
            sortIndicatorOrder = Qt.DescendingOrder;
        }
    }

    TableViewColumn {
        role: parentModel.nameRole
        //% "Comment"
        title: qsTrId("general-comment")
        width: 150 * rootControl.columnResizeRatio
        resizable: false
        movable: false
    }

    TableViewColumn {
        role: parentModel.addressRole
        //% "Address"
        title: qsTrId("general-address")
        width: 150 *  rootControl.columnResizeRatio
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
                    color: Style.content_main
                    copyMenuEnabled: true
                    onCopyText: BeamGlobals.copyToClipboard(text)
                }
            }
        }
    }

    TableViewColumn {
        role: parentModel.categoryRole
        //% "Category"
        title: qsTrId("general-category")
        width: 150 *  rootControl.columnResizeRatio
        resizable: false
        movable: false
    }

    TableViewColumn {
        role: parentModel.expirationRole
        //% "Expiration date"
        title: qsTrId("general-exp-date")
        width: 150 *  rootControl.columnResizeRatio
        resizable: false
        movable: false
        delegate: Item {
            Item {
                width: parent.width
                height: rootControl.rowHeight

                SFText {
                    font.pixelSize: 14
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                    text: Utils.formatDateTime(styleData.value, BeamGlobals.getLocaleName())
                    color: Style.content_main
                }
            }
        }
    }

    TableViewColumn {
        id: createdColumn
        role:parentModel.createdRole
        //% "Created"
        title: qsTrId("general-created")
        width: rootControl.getAdjustedColumnWidth(createdColumn)//150 *  rootControl.columnResizeRatio
        resizable: false
        movable: false
        delegate: Item {
            Item {
                width: parent.width
                height: rootControl.rowHeight

                SFText {
                    font.pixelSize: 14
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                    text: Utils.formatDateTime(styleData.value, BeamGlobals.getLocaleName())
                    color: Style.content_main
                }
            }
        }
    }

    TableViewColumn {
        id: actions
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

            color: styleData.selected ? Style.row_selected :
                    (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: {
                if (mouse.button == Qt.RightButton && styleData.row != undefined)
                {
                    contextMenu.address = rootControl.model[styleData.row].address;
                    contextMenu.addressItem = rootControl.model[styleData.row];
                    contextMenu.popup();
                }
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
                        //% "Actions"
                        ToolTip.text: qsTrId("general-actions")
                        onClicked: {
                            contextMenu.address = rootControl.model[styleData.row].address;
                            contextMenu.addressItem = rootControl.model[styleData.row];
                            contextMenu.popup();
                        }
                    }
                }
            }
        }
    }

    ContextMenu {
        id: contextMenu
        modal: true
        dim: false
        property string address
        property var addressItem
        Action {
            id: showQRAction
            //: Entry in adress table context menu to show QR
            //% "Show QR code"
            text: qsTrId("address-table-cm-show-qr")
            icon.source: "qrc:/assets/icon-qr.svg"
            onTriggered: {
                showQRDialog.addressItem = contextMenu.addressItem;
                showQRDialog.open();
            }
        }
        Action {
            //: Entry in adress table context menu to edit
            //% "Edit address"
            text: qsTrId("address-table-cm-edit")
            icon.source: "qrc:/assets/icon-edit.svg"
            onTriggered: {
                editDialog.addressItem = contextMenu.addressItem;
                editDialog.reset();
                editDialog.open();
            }
        }
        Action {
            //: Entry in adress table context menu to delete
            //% "Delete address"
            text: qsTrId("address-table-cm-delete")
            icon.source: "qrc:/assets/icon-delete.svg"
            onTriggered: {
                viewModel.deleteAddress(contextMenu.address);
            }
        }
    
        Component.onCompleted: {
            if (isExpired) {
                contextMenu.removeAction(showQRAction);
            }
        }
    }
}
