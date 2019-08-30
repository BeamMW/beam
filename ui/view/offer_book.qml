import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0;

Item {
    id: offersViewRoot
    Layout.fillWidth: true
    Layout.fillHeight: true

    property var copiedTxParams

    SwapOffersViewModel {
        id: viewModel
    }

    SFText {
        font.pixelSize: 36
        color: Style.content_main
        //% "Offer Book"
        text: qsTrId("offer-book-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    Component {
        id: offersViewComponent

        ColumnLayout {
            id: offersLayout
            Layout.fillWidth: true
            Layout.fillHeight: true

            state: "all"

            RowLayout {
                Layout.alignment: Qt.AlignRight | Qt.AlignTop
                Layout.topMargin: 33
                
                CustomButton {
                    id: sendOfferButton
                    height: 32
                    palette.button: Style.active
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Create an offer"
                    text: qsTrId("offer-book-create")
                    font.pixelSize: 12
                    font.capitalization: Font.AllUppercase

                    onClicked: {
                        offersStackView.push(Qt.createComponent("receive.qml"), {"isSwapMode": true});
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.bottomMargin: 0

                SFText {
                    Layout.alignment: Qt.AlignTop

                    font.pixelSize: 14
                    font.letterSpacing: 4
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    color: Style.content_main
                    opacity: 0.5
                    //% "Active offers"
                    text: qsTrId("offer-book-title-2")
                }

                SFText {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    Layout.leftMargin: 60

                    font.pixelSize: 12
                    font.letterSpacing: 0.4
                    color: Style.content_main
                    opacity: 0.5
                    //% "Coins"
                    text: qsTrId("offer-book-coins")
                }
                
                CustomComboBox {
                    id: coinTypeComboBox
                    
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2

                    height: 32
                    Layout.preferredWidth: 60
                    fontPixelSize: 12
                    fontLetterSpacing: 0.4
                    color: Style.content_main

                    currentIndex: viewModel.getCoinType()
                    model: ["BTC", "LTC", "QTUM"] // Utils.currenciesList()   // "BEAM", "BTC", "LTC", "QTUM"
                    onCurrentIndexChanged: viewModel.setCoinType(currentIndex)
                }

                // Image {
                //     Layout.alignment: Qt.AlignTop
                //     source: "qrc:/assets/icon-swap-complered.svg"
                // }

                // CustomComboBox {
                //     Layout.alignment: Qt.AlignTop
                //     Layout.topMargin: 2

                //     height: 32
                //     // minimalWidth: 200
                //     fontPixelSize: 12
                //     fontLetterSpacing: 0.4
                //     color: Style.content_main

                //     currentIndex: viewModel.getCoinType() + 1
                //     model: Utils.currenciesList()   // "BEAM", "BTC", "LTC", "QTUM"
                //     onCurrentIndexChanged: viewModel.setCoinType(currentIndex - 1)
                // }

                Item {
                    Layout.fillWidth: true
                }

                TxFilter{
                    id: filterAll
                    Layout.alignment: Qt.AlignTop
                    //% "All"
                    label: qsTrId("offer-book-all-tab")
                    onClicked: offersLayout.state = "all"
                    capitalization: Font.AllUppercase
                }

                TxFilter{
                    id: filterMine
                    Layout.alignment: Qt.AlignTop
                    Layout.leftMargin: 40
                    //% "Mine"
                    label: qsTrId("offer-book-mine-tab")
                    onClicked: offersLayout.state = "mine"
                    capitalization: Font.AllUppercase
                }

                TxFilter{
                    id: filterOthers
                    Layout.alignment: Qt.AlignTop
                    Layout.leftMargin: 40
                    //% "Others"
                    label: qsTrId("offer-book-others-tab")
                    onClicked: offersLayout.state = "others"
                    capitalization: Font.AllUppercase
                }
            }
            
            states: [
                State {
                    name: "all"
                    PropertyChanges { target: filterAll; state: "active" }
                    PropertyChanges { target: proxyModel; filterString: "*" }
                },
                State {
                    name: "mine"
                    PropertyChanges { target: filterMine; state: "active" }
                    PropertyChanges { target: proxyModel; filterString: "true" }
                },
                State {
                    name: "others"
                    PropertyChanges { target: filterOthers; state: "active" }
                    PropertyChanges { target: proxyModel; filterString: "false" }
                }
            ]

            CustomTableView {
                id: tableView

                Layout.alignment: Qt.AlignTop
                Layout.fillWidth : true
                Layout.fillHeight : true
                Layout.topMargin: 15

                property int rowHeight: 69
                property int columnWidth: width / 6

                frameVisible: false
                selectionMode: SelectionMode.NoSelection
                backgroundVisible: false
                sortIndicatorVisible: true
                sortIndicatorColumn: 0
                sortIndicatorOrder: Qt.DescendingOrder

                model: SortFilterProxyModel {
                    id: proxyModel
                    source: viewModel.allOffers

                    sortOrder: tableView.sortIndicatorOrder
                    sortCaseSensitivity: Qt.CaseInsensitive
                    sortRole: tableView.getColumn(tableView.sortIndicatorColumn).role + "Sort"

                    filterRole: "isOwnOffer"
                    // filterString: "*"
                    filterSyntax: SortFilterProxyModel.Wildcard
                    filterCaseSensitivity: Qt.CaseInsensitive
                }

                TableViewColumn {
                    role: "timeCreated"
                    //% "Date | time"
                    title: qsTrId("offer-book-time-created")
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                }

                TableViewColumn {
                    role: "amountSend"
                    //% "Amount"
                    title: qsTrId("offer-book-amount")
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                }

                TableViewColumn {
                    role: "amountReceive"
                    //% "Amount"
                    title: qsTrId("offer-book-amount-swap")
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                }

                TableViewColumn {
                    role: "rate"
                    //% "Rate"
                    title: qsTrId("offer-book-rate")
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                }

                TableViewColumn {
                    role: "expiration"
                    //% "Expiration"
                    title: qsTrId("offer-book-expiration")
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                }
                
                TableViewColumn {
                    title: ""
                    width: tableView.columnWidth
                    movable: false
                    resizable: false
                    delegate: Component {
                        id: actions
                        Item {
                            Layout.fillWidth : true
                            Layout.fillHeight : true
                            property var isOwnOffer: tableView.model.get(styleData.row).isOwnOffer

                            SFText {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.rightMargin: 20

                                font.pixelSize: 14
                                color: Style.active
                                text: isOwnOffer
                                                //% "Cancel offer"
                                                ? qsTrId("offer-book-cancel")
                                                //% "Accept offer"
                                                : qsTrId("offer-book-accept")

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    onClicked: {
                                        if (isOwnOffer) {
                                            copiedTxParams = tableView.model.get(styleData.row).rawTxParameters;
                                            viewModel.cancelTx(copiedTxParams);
                                        }
                                        else {
                                            copiedTxParams = tableView.model.get(styleData.row).rawTxParameters;
                                            offersStackView.push(Qt.createComponent("send.qml"), {"isSwapMode": true, "predefinedTxParams": copiedTxParams});
                                        }
                                    }
                                }
                            }
                        }
                    }
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
                }

                itemDelegate: TableItem {
                    text: styleData.value
                    elide: Text.ElideRight
                }
            }
        }
    }
    
    StackView {
        id: offersStackView

        anchors.fill: parent
        initialItem: offersViewComponent

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
