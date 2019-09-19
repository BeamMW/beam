import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.3
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0

Item {
    id: offersViewRoot
    Layout.fillWidth: true
    Layout.fillHeight: true

    SwapOffersViewModel {
        id: viewModel
    }

    ConfirmationDialog {
        id: betaDialog
        //% "Atomic Swap is in BETA"
        title: qsTrId("swap-beta-title")
        //% "I understand"
        okButtonText:        qsTrId("swap-beta-button")
        okButtonIconSource:  "qrc:/assets/icon-done.svg"
        cancelButtonVisible: false
        width: 470
        //% "Atomic Swap functionality is Beta at the moment. We recommend you not to send large amounts."
        text: qsTrId("swap-beta-message")
    }

    Component.onCompleted: {
        if (viewModel.showBetaWarning) {
            betaDialog.open()
        }
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
                spacing: 10

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
                    borderSize: 1
                    visible: !BeamGlobals.haveBtc() || !BeamGlobals.haveLtc() || !BeamGlobals.haveQtum()
                    MouseArea {
                        id:                clickArea
                        anchors.fill:      parent
                        acceptedButtons:   Qt.LeftButton
                        onClicked:         main.openSwapSettings()
                        hoverEnabled:      true
                        onPositionChanged: clickArea.cursorShape = Qt.PointingHandCursor;
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

                TxFilter {
                    id: offersTabSelector
                    Layout.alignment: Qt.AlignTop
                    Layout.leftMargin: 7
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

                Layout.fillWidth:  true
                Layout.fillHeight: true
                Layout.topMargin:  20

                RowLayout {
                    spacing: 0

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
                        opacity: 0.6
                        onClicked: {
                            console.log("todo: send/receive switch pressed");
                        }
                    }

                    SFText {
                        Layout.alignment:  Qt.AlignHCenter | Qt.AlignLeft
                        Layout.leftMargin: 10
                        font.pixelSize: 14
                        color: Style.content_main
                        opacity: 0.6
                        //% "Send BEAM"
                        text: qsTrId("atomic-swap-send-beam")
                    }

                    CustomCheckBox {
                        id: checkboxOnlyMyOffers
                        Layout.alignment:  Qt.AlignHCenter
                        Layout.leftMargin: 60
                        //% "Only my offers"
                        text:           qsTrId("atomic-swap-only-my-offers")
                    }

                    CustomCheckBox {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        Layout.leftMargin: 60
                        //% "Fit my current balance"
                        text: qsTrId("atomic-swap-fit-current-balance")
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
                    property int columnWidth: (width - 66) / 6

                    frameVisible: false
                    selectionMode: SelectionMode.NoSelection
                    backgroundVisible: false
                    sortIndicatorVisible: true
                    sortIndicatorColumn: 1
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

                    TableViewColumn {
                        // role: ""
                        width: 66
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
                        delegate: TableItem {
                            text: styleData.value
                            elide: Text.ElideRight
                            fontWeight: Font.Bold
                        }
                    }

                    TableViewColumn {
                        role: "amountReceive"
                        //% "Receive"
                        title: qsTrId("atomic-swap-amount-receive")
                        width: offersTable.columnWidth
                        movable: false
                        resizable: false
                        delegate: TableItem {
                            text: styleData.value
                            elide: Text.ElideRight
                            fontWeight: Font.Bold
                        }
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
                                    color: isOwnOffer ? "#ffffff" : "#00f6d2"
                                    opacity: isOwnOffer ? 0.5 : 1.0
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
                                                var txID = offersTable.model.get(styleData.row).rawTxID
                                                viewModel.cancelTx(txID);
                                            }
                                            else {
                                                var txParameters = offersTable.model.get(styleData.row).rawTxParameters;
                                                offersStackView.push(Qt.createComponent("send.qml"), {"isSwapMode": true, "predefinedTxParams": txParameters});
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }   // CustomTableView : offersTable
            }   // ColumnLayout : activeOffersTab

            ColumnLayout {
                id: transactionsTab
                visible: false

                Layout.fillWidth:  true
                Layout.fillHeight: true
                Layout.topMargin:  14

                state: "filterAllTransactions"

                RowLayout {

                    TxFilter {
                        id: allTabSelector
                        Layout.rightMargin: 40
                        Layout.leftMargin: 7
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
                        rightPadding: 5
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
                    Layout.topMargin: 12

                    property int rowHeight: 56
                    property int columnWidth: (width - 106) / 6

                    frameVisible: false
                    selectionMode: SelectionMode.NoSelection
                    backgroundVisible: false
                    sortIndicatorVisible: true
                    sortIndicatorColumn: 1
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
                        width: 66
                        movable: false
                        resizable: false
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
                        delegate: TableItem {
                            text: (styleData.value === '' ? '' : '-') + styleData.value
                            elide: Text.ElideRight
                            fontWeight: Font.Bold
                            color: "#da68f5"
                        }
                    }
                    TableViewColumn {
                        role: "amountReceive"
                        //% "Received"
                        title: qsTrId("atomic-swap-tx-table-received")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                        delegate: TableItem {
                            text: (styleData.value === '' ? '' : '+') + styleData.value
                            elide: Text.ElideRight
                            fontWeight: Font.Bold
                            color: "#0bccf7"
                        }
                    }
                    TableViewColumn {
                        role: "status"
                        //% "Status"
                        title: qsTrId("atomic-swap-tx-table-status")
                        width: transactionsTable.columnWidth
                        movable: false
                        resizable: false
                        delegate: Item {
                            width: parent.width
                            height: transactionsTable.rowHeight

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                // spacing: 14

                                SvgImage {
                                    Layout.alignment: Qt.AlignHCenter
                                    sourceSize: Qt.size(20, 20)
                                    source: getIconSource()

                                    function getIconSource() {
                                        if (transactionsTable.model.get(styleData.row).isSelfTransaction) {
                                            return "qrc:/assets/icon-transfer.svg";
                                        }
                                        return transactionsTable.model.get(styleData.row).isIncome ?
                                            "qrc:/assets/icon-received.svg" :
                                            "qrc:/assets/icon-sent.svg";
                                    }
                                }

                                SFLabel {
                                    Layout.alignment: Qt.AlignHCenter
                                    font.pixelSize: 14
                                    font.italic: true
                                    elide: Text.ElideRight
                                    text: getStatusText(styleData.value)
                                    color: getTextColor()

                                    function getTextColor () {
                                        if (transactionsTable.model.get(styleData.row).IsInProgress || transactionsTable.model.get(styleData.row).IsCompleted) {
                                            if (transactionsTable.model.get(styleData.row).isSelfTransaction) {
                                                return Style.content_main;
                                            }
                                            return transactionsTable.model.get(styleData.row).isIncome ? Style.accent_incoming : Style.accent_outgoing;
                                        }

                                        return Style.content_main;
                                    }

                                    function getStatusText(value) {
                                        switch(value) {
                                            //% "pending"
                                            case "pending": return qsTrId("wallet-txs-status-pending");
                                            //% "waiting for sender"
                                            case "waiting for sender": return qsTrId("wallet-txs-status-waiting-sender");
                                            //% "waiting for receiver"
                                            case "waiting for receiver": return qsTrId("wallet-txs-status-waiting-receiver");
                                            //% "receiving"
                                            case "receiving": return qsTrId("general-receiving");
                                            //% "sending"
                                            case "sending": return qsTrId("general-sending");
                                            //% "completed"
                                            case "completed": return qsTrId("wallet-txs-status-completed");
                                            //% "received"
                                            case "received": return qsTrId("wallet-txs-status-received");
                                            //% "sent"
                                            case "sent": return qsTrId("wallet-txs-status-sent");
                                            //% "cancelled"
                                            case "cancelled": return qsTrId("wallet-txs-status-cancelled");
                                            //% "expired"
                                            case "expired": return qsTrId("wallet-txs-status-expired");
                                            //% "failed"
                                            case "failed": return qsTrId("wallet-txs-status-failed");
                                            //% "unknown"
                                            default: return qsTrId("wallet-txs-status-unknown");
                                        }
                                    }
                                }
                            }
                        }
                    }

                    TableViewColumn {
                        id: actionsColumn
                        title: ""
                        width: 40
                        movable: false
                        resizable: false
                        delegate: txActions
                    }

                    Component {
                        id: txActions
                        Item {
                            Item {
                                width: parent.width
                                height: transactionsTable.rowHeight

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
                                            txContextMenu.address = transactionsTable.model.get(styleData.row).addressTo;
                                            txContextMenu.cancelEnabled = transactionsTable.model.get(styleData.row).isCancelAvailable;
                                            txContextMenu.deleteEnabled = transactionsTable.model.get(styleData.row).isDeleteAvailable;
                                            txContextMenu.txID = transactionsTable.model.get(styleData.row).rawTxID;
                                            txContextMenu.popup();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }   // CustomTableView : transactionsTable

                ContextMenu {
                    id: txContextMenu
                    modal: true
                    dim: false
                    property bool cancelEnabled
                    property bool deleteEnabled
                    property var address
                    property var txID
                    Action {
                        //% "Copy address"
                        text: qsTrId("wallet-txs-copy-addr-cm")
                        icon.source: "qrc:/assets/icon-copy.svg"
                        onTriggered: {
                            BeamGlobals.copyToClipboard(txContextMenu.address);
                        }
                    }
                    Action {
                        //% "Cancel"
                        text: qsTrId("general-cancel")
                        icon.source: "qrc:/assets/icon-cancel.svg"
                        enabled: txContextMenu.cancelEnabled
                        onTriggered: {
                            viewModel.cancelTx(txContextMenu.txID);
                        }
                    }
                    Action {
                        //% "Delete"
                        text: qsTrId("general-delete")
                        icon.source: "qrc:/assets/icon-delete.svg"
                        enabled: txContextMenu.deleteEnabled
                        onTriggered: {
                            //% "The transaction will be deleted. This operation can not be undone"
                            deleteTransactionDialog.text = qsTrId("wallet-txs-delete-message");
                            deleteTransactionDialog.open();
                        }
                    }
                    Connections {
                        target: deleteTransactionDialog
                        onAccepted: {
                            viewModel.deleteTx(txContextMenu.txID);
                        }
                    }
                }
                
                ConfirmationDialog {
                    id: deleteTransactionDialog
                    //% "Delete"
                    okButtonText: qsTrId("general-delete")
                }
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
