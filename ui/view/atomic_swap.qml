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

    RowLayout {
        SFText {
            font.pixelSize: 36
            color: Style.content_main
            //% "Atomic Swap"
            text: qsTrId("atomic-swap-title")
        }

        SvgImage {
            Layout.alignment: Qt.AlignLeft | Qt.AlignHCenter
            Layout.maximumHeight: 15
            Layout.maximumWidth: 42
            source: "qrc:/assets/beta-label.svg"
        }
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    Component {
        id: offersViewComponent

        ColumnLayout {
            id: atomicSwapLayout
            Layout.fillWidth: true
            Layout.fillHeight: true

            state: "offers"

            RowLayout {
                Layout.alignment: Qt.AlignRight | Qt.AlignTop
                Layout.topMargin: 33
                
                CustomButton {
                    id: sendOfferButton
                    Layout.minimumWidth: 172
                    Layout.minimumHeight: 32
                    palette.button: Style.active
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Create offer"
                    text: qsTrId("atomic-swap-create")
                    font.pixelSize: 12
                    font.capitalization: Font.AllUppercase

                    onClicked: {
                        offersStackView.push(Qt.createComponent("receive.qml"), {"isSwapMode": true});
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.topMargin: 30
                spacing: 5

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftBEAM
                    currencyIcon: "qrc:/assets/icon-beam.svg"
                    valueStr: viewModel.beamAvailable + " " + Utils.symbolBeam
                    visible: true
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftBTC
                    currencyIcon: "qrc:/assets/icon-btc.svg"
                    valueStr: viewModel.btcAvailable + " " + Utils.symbolBtc
                    isOk: viewModel.btcOK
                    visible: BeamGlobals.haveBtc()
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftLTC
                    currencyIcon: "qrc:/assets/icon-ltc.svg"
                    valueStr: viewModel.ltcAvailable + " " + Utils.symbolLtc
                    isOk: viewModel.ltcOK
                    visible: BeamGlobals.haveLtc()
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftQTUM
                    currencyIcon: "qrc:/assets/icon-qtum.svg"
                    valueStr: viewModel.qtumAvailable + " " + Utils.symbolQtum
                    isOk: viewModel.qtumOK
                    visible: BeamGlobals.haveQtum()
                }

                SwapCurrencyAmountPane {
                    id: swapOptions
                    gradLeft: Style.swapCurrencyPaneGrLeftOther
                    gradRight: Style.swapCurrencyPaneGrLeftOther
                    //% "Connect other currency wallet to start trading"
                    valueStr: qsTrId("atomic-swap-connect-other")
                    textSize: 14
                    textColor: Style.active
                    isOk: true
                    visible: !BeamGlobals.haveBtc() || !BeamGlobals.haveLtc() || !BeamGlobals.haveQtum()
                    onClick: function() {
                        main.openSwapSettings();
                    }
                }
                Component.onCompleted: {
                    var currencyIcons = [];
                    if (!BeamGlobals.haveBtc())
                        currencyIcons.push("qrc:/assets/icon-btc.svg");
                    if (!BeamGlobals.haveLtc())
                        currencyIcons.push("qrc:/assets/icon-ltc.svg");
                    if (!BeamGlobals.haveQtum())
                        currencyIcons.push("qrc:/assets/icon-qtum.svg");

                    swapOptions.currencyIcons = currencyIcons;
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.topMargin: 50
                // Layout.bottomMargin: 20

                TxFilter {
                    id: offersTabSelector
                    Layout.alignment: Qt.AlignTop
                    //% "Active offers"
                    label: qsTrId("atomic-swap-active-offers-tab")
                    onClicked: atomicSwapLayout.state = "offers"
                    capitalization: Font.AllUppercase
                }

                TxFilter {
                    id: transactionsTabSelector
                    Layout.alignment: Qt.AlignTop
                    Layout.leftMargin: 40
                    //% "Transactions"
                    label: qsTrId("atomic-swap-transactions-tab")
                    onClicked: atomicSwapLayout.state = "transactions"
                    capitalization: Font.AllUppercase
                }
            }
            
            states: [
                State {
                    name: "offers"
                    PropertyChanges { target: offersTabSelector; state: "active" }
                    PropertyChanges { target: activeOffersTab; visible: true }
                    PropertyChanges { target: transactionsTab; visible: false }
                },
                State {
                    name: "transactions"
                    PropertyChanges { target: transactionsTabSelector; state: "active" }
                    PropertyChanges { target: activeOffersTab; visible: false }
                    PropertyChanges { target: transactionsTab; visible: true }
                }
            ]

            ColumnLayout {
                id: activeOffersTab
                visible: false

                Layout.fillWidth : true
                Layout.fillHeight : true

                RowLayout {
                    
                    SFText {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        font.pixelSize: 14
                        color: Style.content_main
                        opacity: 0.6
                        //% "Receive BEAM"
                        text: qsTrId("atomic-swap-receive-beam")
                    }

                    CustomSwitch {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        onClicked: {
                            console.log("todo: send/receive switch pressed");
                        }
                    }

                    SFText {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        font.pixelSize: 14
                        color: Style.content_main
                        opacity: 0.6
                        //% "Send BEAM"
                        text: qsTrId("atomic-swap-send-beam")
                    }

                    CustomCheckBox {
                        id: checkboxOnlyMyOffers
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        Layout.leftMargin: 60
                        //% "Only my offers"
                        text: qsTrId("atomic-swap-only-my-offers")
                        // opacity: 0.5
                        // font.pixelSize: 14
                        // color: Style.content_main
                    }

                    CustomCheckBox {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        Layout.leftMargin: 60
                        //% "Fit my current balance"
                        text: qsTrId("atomic-swap-fit-current-balance")
                        // opacity: 0.5
                        // font.pixelSize: 14
                        // color: Style.content_main
                        onClicked: {
                            console.log("todo: fit current balance checkbox pressed");
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    SFText {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignRight
                        Layout.rightMargin: 10
                        font.pixelSize: 14
                        color: Style.content_main
                        opacity: 0.5
                        //% "Currency"
                        text: qsTrId("atomic-swap-currency")
                    }
                    
                    CustomComboBox {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignRight
                        height: 32
                        Layout.preferredWidth: 65
                        fontPixelSize: 14
                        fontLetterSpacing: 0.47
                        color: Style.content_main

                        currentIndex: viewModel.getCoinType()
                        model: ["BTC", "LTC", "QTUM"]
                        onCurrentIndexChanged: viewModel.setCoinType(currentIndex)
                    }
                }   // RowLayout

                CustomTableView {
                    id: offersTable

                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth : true
                    Layout.fillHeight : true
                    Layout.topMargin: 14

                    property int rowHeight: 56
                    property int columnWidth: (width - 76) / 6

                    frameVisible: false
                    selectionMode: SelectionMode.NoSelection
                    backgroundVisible: false
                    sortIndicatorVisible: true
                    sortIndicatorColumn: 0
                    sortIndicatorOrder: Qt.DescendingOrder

                    model: SortFilterProxyModel {
                        id: proxyModel
                        source: viewModel.allOffers

                        sortOrder: offersTable.sortIndicatorOrder
                        sortCaseSensitivity: Qt.CaseInsensitive
                        sortRole: offersTable.getColumn(offersTable.sortIndicatorColumn).role + "Sort"

                        filterRole: "isOwnOffer"
                        filterString: checkboxOnlyMyOffers.checked ? "true" : "*"
                        filterSyntax: SortFilterProxyModel.Wildcard
                        filterCaseSensitivity: Qt.CaseInsensitive
                    }

                    TableViewColumn {
                        // role: ""
                        width: 76
                        movable: false
                        resizable: false
                    }

                    TableViewColumn {
                        role: "timeCreated"
                        //% "Created on"
                        title: qsTrId("atomic-swap-time-created")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                    }

                    TableViewColumn {
                        role: "amountSend"
                        //% "Send"
                        title: qsTrId("atomic-swap-amount-send")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                    }

                    TableViewColumn {
                        role: "amountReceive"
                        //% "Receive"
                        title: qsTrId("atomic-swap-amount-receive")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                    }

                    TableViewColumn {
                        role: "rate"
                        //% "Rate"
                        title: qsTrId("atomic-swap-rate")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                    }

                    TableViewColumn {
                        role: "expiration"
                        //% "Expiration"
                        title: qsTrId("atomic-swap-expiration")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    
                    TableViewColumn {
                        title: ""
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                        delegate: Component {
                            id: actions
                            Item {
                                Layout.fillWidth : true
                                Layout.fillHeight : true
                                property var isOwnOffer: offersTable.model.get(styleData.row).isOwnOffer

                                SFText {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: 20

                                    font.pixelSize: 14
                                    color: Style.active
                                    text: isOwnOffer
                                                    //% "Cancel offer"
                                                    ? qsTrId("atomic-swap-cancel")
                                                    //% "Accept offer"
                                                    : qsTrId("atomic-swap-accept")

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        onClicked: {
                                            if (isOwnOffer) {
                                                copiedTxParams = offersTable.model.get(styleData.row).rawTxParameters;
                                                viewModel.cancelTx(copiedTxParams);
                                            }
                                            else {
                                                copiedTxParams = offersTable.model.get(styleData.row).rawTxParameters;
                                                offersStackView.push(Qt.createComponent("send.qml"), {"isSwapMode": true, "predefinedTxParams": copiedTxParams});
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    rowDelegate: Item {
                        height: offersTable.rowHeight
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
                }   // CustomTableView : offersTable
            }   // ColumnLayout : activeOffersTab

            ColumnLayout {
                id: transactionsTab
                visible: false

                Layout.fillWidth : true
                Layout.fillHeight : true

                state: "filterAllTransactions"

                RowLayout {

                    TxFilter {
                        id: allTabSelector
                        Layout.rightMargin: 40
                        //% "All"
                        label: qsTrId("atomic-swap-all-transactions-tab")
                        onClicked: transactionsTab.state = "filterAllTransactions"
                        capitalization: Font.AllUppercase
                    }

                    TxFilter {
                        id: inProgressTabSelector
                        //% "In progress"
                        label: qsTrId("atomic-swap-in-progress-transactions-tab")
                        onClicked: transactionsTab.state = "filterInProgressTransactions"
                        capitalization: Font.AllUppercase
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    CustomButton {
                        Layout.alignment: Qt.AlignRight
                        // Layout.minimumHeight: 20
                        // Layout.minimumWidth: 20
                        // shadowRadius: 5
                        // shadowSamples: 7
                        // Layout.margins: shadowRadius
                        // leftPadding: 5
                        // rightPadding: 5
                        textOpacity: 0
                        icon.source: "qrc:/assets/icon-delete.svg"
                        // enabled: localNodeRun.checked
                        onClicked: console.log("todo: delete button pressed");
                    }
                }

                states: [
                    State {
                        name: "filterAllTransactions"
                        PropertyChanges { target: allTabSelector; state: "active" }
                        PropertyChanges { target: txProxyModel; filterString: "*" }
                    },
                    State {
                        name: "filterInProgressTransactions"
                        PropertyChanges { target: inProgressTabSelector; state: "active" }
                        PropertyChanges { target: txProxyModel; filterString: "pending" } // "in progress" - should be
                    }
                ]

                CustomTableView {
                    id: transactionsTable

                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 14

                    property int rowHeight: 56
                    property int columnWidth: width / 6

                    frameVisible: false
                    selectionMode: SelectionMode.NoSelection
                    backgroundVisible: false
                    sortIndicatorVisible: true
                    sortIndicatorColumn: 0
                    sortIndicatorOrder: Qt.DescendingOrder

                    model: SortFilterProxyModel {
                        id: txProxyModel
                        source: viewModel.transactions

                        sortOrder: transactionsTable.sortIndicatorOrder
                        sortCaseSensitivity: Qt.CaseInsensitive
                        sortRole: transactionsTable.getColumn(transactionsTable.sortIndicatorColumn).role + "Sort"

                        filterRole: "status"
                        // filterString: "*"
                        filterSyntax: SortFilterProxyModel.Wildcard
                        filterCaseSensitivity: Qt.CaseInsensitive
                    }

                    rowDelegate: Item {
                        height: transactionsTable.rowHeight
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

                    TableViewColumn {
                        role: "timeCreated"
                        //% "Created on"
                        title: qsTrId("atomic-swap-tx-table-created")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    TableViewColumn {
                        role: "addressFrom"
                        //% "From"
                        title: qsTrId("atomic-swap-tx-table-from")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    TableViewColumn {
                        role: "addressTo"
                        //% "To"
                        title: qsTrId("atomic-swap-tx-table-to")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    TableViewColumn {
                        role: "amountSend"
                        //% "Sent"
                        title: qsTrId("atomic-swap-tx-table-sent")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    TableViewColumn {
                        role: "amountReceive"
                        //% "Received"
                        title: qsTrId("atomic-swap-tx-table-received")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                    TableViewColumn {
                        role: "status"
                        //% "Status"
                        title: qsTrId("atomic-swap-tx-table-status")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                    }
                }   // CustomTableView : transactionsTable
            }   // ColumnLayout : transactionsTab
        } // ColumnLayout : 
    } // Component : offersViewComponent
    
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
