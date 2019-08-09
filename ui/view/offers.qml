import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "controls"
import Beam.Wallet 1.0;

Item {
    id: offersViewRoot
    anchors.fill: parent

    property var copiedTxParams

    SwapOffersViewModel {
        id: viewModel
    }

    SFText {
        font.pixelSize: 36
        color: Style.content_main
        text: qsTrId("offers-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    Component {
        id: offersViewLayout

        Item {

            Row {
                anchors.top: parent.top
                anchors.right: parent.right
                spacing: 19
                
                SFTextInput {
                    id: searchBox
                    anchors.topMargin: 120
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.content_main
                    placeholderText: qsTrId("offers-search")
                    inputMethodHints: Qt.ImhNoPredictiveText
                }

                CustomComboBox {
                    id: coinTypeComboBox
                    fontPixelSize: 18
                    color: Style.content_main

                    currentIndex: viewModel.getCoinType()

                    model: [
                        "Bitcoin",
                        "Litecoin",
                        "Qtum"
                    ]

                    onCurrentIndexChanged: viewModel.setCoinType(currentIndex)
                }
                
                CustomButton {
                    id: sendOfferButton
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    text: qsTrId("offers-create")

                    onClicked: {
                        offersStackView.push(Qt.createComponent("receive.qml"), {"isSwapMode": true});
                    }
                }
            }

            CustomTableView {
                id: tableView
                property int rowHeight: 69

                anchors.fill: parent
                anchors.topMargin: 120-33
                Layout.bottomMargin: 9

                frameVisible: false
                selectionMode: SelectionMode.NoSelection
                backgroundVisible: false
                sortIndicatorVisible: true
                sortIndicatorColumn: 1
                sortIndicatorOrder: Qt.DescendingOrder

                model: SortFilterProxyModel {
                    id: proxyModel
                    source: viewModel.allOffers

                    sortOrder: tableView.sortIndicatorOrder
                    sortCaseSensitivity: Qt.CaseInsensitive
                    sortRole: tableView.getColumn(tableView.sortIndicatorColumn).role + "Sort"

                    filterString: "*" + searchBox.text + "*"
                    filterSyntax: SortFilterProxyModel.Wildcard
                    filterCaseSensitivity: Qt.CaseInsensitive
                }

                TableViewColumn {
                    role: "time"
                    title: qsTrId("offers-last-updated")
                    width: parent.width / 5
                    movable: false
                }

                TableViewColumn {
                    role: "id"
                    title: qsTrId("offers-id")
                    width: parent.width / 5
                    movable: false
                }

                TableViewColumn {
                    role: "amount"
                    title: qsTrId("offers-amount")
                    width: parent.width / 5
                    movable: false
                }

                TableViewColumn {
                    role: "amountSwap"
                    title: qsTrId("offers-amount-swap")
                    width: parent.width / 5
                    movable: false
                }

                TableViewColumn {
                    role: "message"
                    title: qsTrId("offers-message")
                    width: parent.width / 5
                    movable: false
                }
                
                rowDelegate: Item {
                    height: tableView.rowHeight
                    anchors.left: parent.left
                    anchors.right: parent.right

                    Rectangle {
                        anchors.fill: parent                        
                        color: styleData.selected ? Style.row_selected : Style.background_row_even
                        visible: styleData.selected ? true : styleData.alternate
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: {
                            var item = tableView.model.get(styleData.row);
                            copiedTxParams = item.rawTxParameters;
                            contextMenu.popup();
                        }
                    }
                }

                itemDelegate: TableItem {
                    text: styleData.value
                    elide: Text.ElideRight
                }

                ContextMenu {
                    id: contextMenu
                    modal: true
                    dim: false
                    Action {
                        text: qsTrId("offers-accept")
                        icon.source: "qrc:/assets/icon-copy.svg"
                        onTriggered: {
                            offersStackView.push(Qt.createComponent("send.qml"), {"isSwapMode": true, "predefinedTxParams": copiedTxParams});
                        }
                    }
                }
            }
        }
    }
    
    StackView {
        id: offersStackView
        anchors.fill: parent
        initialItem: offersViewLayout

        pushEnter: Transition {
            enabled: false
        }
        pushExit: Transition {
            enabled: false
        }
        popEnter: Transition {
            enabled: false
        }
        popExit: Transition {
            enabled: false
        }

        onCurrentItemChanged: {
            if (currentItem && currentItem.defaultFocusItem) {
                offersStackView.currentItem.defaultFocusItem.forceActiveFocus();
            }
        }
    }
}
