import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"
import "utils.js" as Utils

Item {
    id: root
    anchors.fill: parent

    WalletViewModel {
        id: viewModel
    }

    property bool toSend: false

    ConfirmationDialog {
        id: deleteTransactionDialog
        //% "Delete"
        okButtonText: qsTrId("general-delete")
    }

    OpenExternalLinkConfirmation {
        id: externalLinkConfirmation
    }   

    PaymentInfoDialog {
        id: paymentInfoDialog
        onTextCopied: function(text){
            BeamGlobals.copyToClipboard(text);
        }
    }

    PaymentInfoItem {
        id: verifyInfo
    }

    PaymentInfoDialog {
        id: paymentInfoVerifyDialog
        shouldVerify: true
        
        model:verifyInfo 
        onTextCopied: function(text){
            BeamGlobals.copyToClipboard(text);
        }
    }
    
    Title {
        x: 0
        //% "Wallet"
        text: qsTrId("wallet-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    Component {
        id: walletLayout

        ColumnLayout {
            id: transactionsLayout
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            state: "all"
            
            Row {
                Layout.alignment: Qt.AlignTop | Qt.AlignRight
                Layout.topMargin: 33
                spacing: 20

                CustomButton {
                    height: 32
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Send"
                    text: qsTrId("general-send")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        walletView.push(Qt.createComponent("send.qml"));
                    }
                }

                CustomButton {
                    height: 32
                    palette.button: Style.accent_incoming
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-receive-blue.svg"
                    //% "Receive"
                    text: qsTrId("wallet-receive-button")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        walletView.push(Qt.createComponent("receive.qml"), {"isSwapView": false});
                    }
                }
            }

            AvailablePanel {
                Layout.topMargin: 32
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.maximumHeight: 67
                Layout.minimumHeight: 67

                width: viewModel.beamSending > 0 || viewModel.beamReceiving > 0 ? parent.width : (parent.width / 2)

                available:         viewModel.beamAvailable
                locked:            viewModel.beamLocked
                lockedMaturing:    viewModel.beamLockedMaturing
                sending:           viewModel.beamSending
                receiving:         viewModel.beamReceiving
                receivingChange:   viewModel.beamReceivingChange
                receivingIncoming: viewModel.beamReceivingIncoming
            }

            Item {
                Layout.topMargin: 45
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth : true

                SFText {

                    font {
                        pixelSize: 14
                        letterSpacing: 4
                        styleName: "Bold"; weight: Font.Bold
                        capitalization: Font.AllUppercase
                    }

                    opacity: 0.5
                    color: Style.content_main

                    //% "Transactions"
                    text: qsTrId("wallet-transactions-title")
                }
            }
            
            RowLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.topMargin: 30

                TxFilter {
                    id: allTabSelector
                    Layout.alignment: Qt.AlignTop
                    //% "All"
                    label: qsTrId("wallet-transactions-all-tab")
                    onClicked: transactionsLayout.state = "all"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: inProgressTabSelector
                    Layout.alignment: Qt.AlignTop
                    //% "In progress"
                    label: qsTrId("wallet-transactions-in-progress-tab")
                    onClicked: transactionsLayout.state = "inProgress"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: sentTabSelector
                    Layout.alignment: Qt.AlignTop
                    //% "Sent"
                    label: qsTrId("wallet-transactions-sent-tab")
                    onClicked: transactionsLayout.state = "sent"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: receivedTabSelector
                    Layout.alignment: Qt.AlignTop
                    //% "Received"
                    label: qsTrId("wallet-transactions-received-tab")
                    onClicked: transactionsLayout.state = "received"
                    capitalization: Font.AllUppercase
                }
                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                }
                CustomToolButton {
                    Layout.alignment: Qt.AlignTop | Qt.AlignRight
                    icon.source: "qrc:/assets/icon-proof.svg"
                    //% "Verify payment"
                    ToolTip.text: qsTrId("wallet-verify-payment")
                    onClicked: {
                        paymentInfoVerifyDialog.model.reset();
                        paymentInfoVerifyDialog.open();
                    }
                }
            }
            
            states: [
                State {
                    name: "all"
                    PropertyChanges { target: allTabSelector; state: "active" }
                    PropertyChanges { target: txProxyModel; filterRole: "status" }
                    PropertyChanges { target: txProxyModel; filterString: "*" }
                },
                State {
                    name: "inProgress"
                    PropertyChanges { target: inProgressTabSelector; state: "active" }
                    PropertyChanges { target: txProxyModel; filterRole: "isInProgress" }
                    PropertyChanges { target: txProxyModel; filterString: "true" }
                },
                State {
                    name: "sent"
                    PropertyChanges { target: sentTabSelector; state: "active" }
                    PropertyChanges { target: txProxyModel; filterRole: "status" }
                    PropertyChanges { target: txProxyModel; filterString: "sent" }
                },
                State {
                    name: "received"
                    PropertyChanges { target: receivedTabSelector; state: "active" }
                    PropertyChanges { target: txProxyModel; filterRole: "status" }
                    PropertyChanges { target: txProxyModel; filterString: "received" }
                }
            ]

            CustomTableView {
                id: transactionsTable

                Layout.alignment: Qt.AlignTop
                Layout.fillWidth : true
                Layout.fillHeight : true
                Layout.bottomMargin: 9

                property int rowHeight: 56

                property double resizableWidth: transactionsTable.width - actionsColumn.width
                property double columnResizeRatio: resizableWidth / 810

                selectionMode: SelectionMode.NoSelection
                sortIndicatorVisible: true
                sortIndicatorColumn: 0
                sortIndicatorOrder: Qt.DescendingOrder

                onSortIndicatorColumnChanged: {
                    sortIndicatorOrder = sortIndicatorColumn != 0
                        ? Qt.AscendingOrder
                        : Qt.DescendingOrder;
                }

                model: SortFilterProxyModel {
                    id: txProxyModel
                    source: viewModel.transactions

                    sortOrder: transactionsTable.sortIndicatorOrder
                    sortCaseSensitivity: Qt.CaseInsensitive
                    sortRole: transactionsTable.getColumn(transactionsTable.sortIndicatorColumn).role + "Sort"

                    filterRole: "timeCreated"
                    // filterString: "*"
                    filterSyntax: SortFilterProxyModel.Wildcard
                    filterCaseSensitivity: Qt.CaseInsensitive
                }

                rowDelegate: Item {
                    id: rowItem
                    height: transactionsTable.rowHeight
                    anchors.left: parent.left
                    anchors.right: parent.right
                    property bool collapsed: true

                    Rectangle {
                        anchors.fill: parent                        
                        color: styleData.selected ? Style.row_selected :
                                (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
                    }

                    ColumnLayout {
                        id: rowColumn
                        width: parent.width
                        Rectangle {
                            height: rowItem.height
                            width: parent.width
                            color: "transparent"
                        }
                        Item {
                            id: txDetails
                            height: 0
                            width: parent.width
                            clip: true

                            property int maximumHeight: detailsPanel.height

                            onMaximumHeightChanged: {
                                if (!rowItem.collapsed) {
                                    rowItem.height = maximumHeight + rowItem.height
                                    txDetails.height = maximumHeight
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: Style.background_details
                            }
                            TransactionDetails {
                                id: detailsPanel
                                width: transactionsTable.width

                                property var txRolesMap: transactionsTable.model.get(styleData.row)
                                sendAddress:        txRolesMap.addressFrom ? txRolesMap.addressFrom : ""
                                receiveAddress:     txRolesMap.addressTo ? txRolesMap.addressTo : ""
                                fee:                txRolesMap.fee ? txRolesMap.fee : ""
                                comment:            txRolesMap.comment ? txRolesMap.comment : ""
                                txID:               txRolesMap.txID ? txRolesMap.txID : ""
                                kernelID:           txRolesMap.kernelID ? txRolesMap.kernelID : ""
                                status:             txRolesMap.status ? txRolesMap.status : ""
                                failureReason:      txRolesMap.failureReason ? txRolesMap.failureReason : ""
                                isIncome:           txRolesMap.isIncome ? txRolesMap.isIncome : false
                                hasPaymentProof:    txRolesMap.hasPaymentProof ? txRolesMap.hasPaymentProof : false
                                isSelfTx:           txRolesMap.isSelfTransaction ? txRolesMap.isSelfTransaction : false
                                rawTxID:            txRolesMap.rawTxID ? txRolesMap.rawTxID : null
                                
                                onOpenExternal : function() {
                                    var url = Style.explorerUrl + "block?kernel_id=" + detailsPanel.kernelID;
                                    Utils.openExternal(url, viewModel, externalLinkConfirmation);
                                }
                                onTextCopied: function (text) {
                                    BeamGlobals.copyToClipboard(text);
                                }
                                onCopyPaymentProof: function() {
                                    if (detailsPanel.rawTxID)
                                    {
                                        var paymentInfo = viewModel.getPaymentInfo(detailsPanel.rawTxID);
                                        if (paymentInfo.paymentProof.length == 0)
                                        {
                                            paymentInfo.paymentProofChanged.connect(function() {
                                                textCopied(paymentInfo.paymentProof);
                                            });
                                        }
                                        else
                                        {
                                            textCopied(paymentInfo.paymentProof);
                                        }
                                    }
                                }
                                onShowPaymentProof: {
                                    if (detailsPanel.rawTxID)
                                    {
                                        paymentInfoDialog.model = viewModel.getPaymentInfo(detailsPanel.rawTxID);
                                        paymentInfoDialog.open();
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        height: transactionsTable.rowHeight
                        width: parent.width

                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: {
                            if (styleData.row === undefined 
                            || styleData.row < 0
                            || styleData.row >= txProxyModel.count)
                            {
                                return;
                            }
                            if (mouse.button === Qt.RightButton )
                            {
                                var item = transactionsTable.model.get(styleData.row);
                                txContextMenu.cancelEnabled = item.isCancelAvailable;
                                txContextMenu.deleteEnabled = item.isDeleteAvailable;
                                txContextMenu.txID = item.rawTxID;
                                txContextMenu.popup();
                            }
                            else if (mouse.button === Qt.LeftButton)
                            {
                                if (parent.collapsed)
                                {
                                    expand.start()
                                }
                                else 
                                {
                                    collapse.start()
                                }
                                parent.collapsed = !parent.collapsed;
                            }
                        }
                    }

                    ParallelAnimation {
                        id: expand
                        running: false

                        property int expandDuration: 200

                        NumberAnimation {
                            target: rowItem
                            easing.type: Easing.Linear
                            property: "height"
                            to: rowItem.height + txDetails.maximumHeight
                            duration: expand.expandDuration
                        }

                        NumberAnimation {
                            target: txDetails
                            easing.type: Easing.Linear
                            property: "height"
                            to: txDetails.maximumHeight
                            duration: expand.expandDuration
                        }
                    }

                    ParallelAnimation {
                        id: collapse
                        running: false

                        property int collapseDuration: 200

                        NumberAnimation {
                            target: rowItem
                            easing.type: Easing.Linear
                            property: "height"
                            to: transactionsTable.rowHeight
                            duration: collapse.collapseDuration
                        }

                        NumberAnimation {
                            target: txDetails
                            easing.type: Easing.Linear
                            property: "height"
                            to: 0
                            duration: collapse.collapseDuration
                        }
                    }
                }

                itemDelegate: Item {
                    Item {
                        width: parent.width
                        height: transactionsTable.rowHeight

                        TableItem {
                            text: styleData.value
                            elide: styleData.elideMode
                            onCopyText: BeamGlobals.copyToClipboard(styleData.value)
                        }
                    }
                }

                TableViewColumn {
                    role: "timeCreated"
                    //% "Created on"
                    title: qsTrId("wallet-txs-date-time")
                    elideMode: Text.ElideRight
                    width: 120 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                }
                TableViewColumn {
                    role: "addressFrom"
                    //% "From"
                    title: qsTrId("general-address-from")
                    elideMode: Text.ElideMiddle
                    width: 205 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                }
                TableViewColumn {
                    role: "addressTo"
                    //% "To"
                    title: qsTrId("general-address-to")
                    elideMode: Text.ElideMiddle
                    width: 205 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                }
                TableViewColumn {
                    role: "amountGeneral"
                    //% "Amount"
                    title: qsTrId("general-amount")
                    elideMode: Text.ElideRight
                    width: 130 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsTable.rowHeight
                            property var isIncome: transactionsTable.model.get(styleData.row).isIncome
                            TableItem {
                                text: (parent.isIncome ? "+ " : "- ") + styleData.value
                                fontWeight: Font.Bold
                                color: parent.isIncome ? Style.accent_incoming : Style.accent_outgoing
                                onCopyText: BeamGlobals.copyToClipboard(Utils.getAmountWithoutCurrency(styleData.value)) 
                            }
                        }
                    }
                }
                TableViewColumn {
                    id: statusColumn
                    role: "status"
                    //% "Status"
                    title: qsTrId("general-status")
                    elideMode: Text.ElideRight
                    width: transactionsTable.getAdjustedColumnWidth(statusColumn)//150 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsTable.rowHeight

                            RowLayout {
                                Layout.alignment: Qt.AlignLeft
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                spacing: 10

                                SvgImage {
                                    Layout.alignment: Qt.AlignLeft
                                    
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
                                    Layout.alignment: Qt.AlignLeft
                                    
                                    font.pixelSize: 14
                                    font.italic: true
                                    elide: Text.ElideRight
                                    text: getStatusText(styleData.value)
                                    color: getTextColor()
                                    function getTextColor () {
                                        var item = transactionsTable.model.get(styleData.row);                                        
                                        if (item.isInProgress || item.isCompleted) {
                                            if (item.isSelfTransaction) {
                                                return Style.content_main;
                                            }
                                            return item.isIncome ? Style.accent_incoming : Style.accent_outgoing;
                                        }
                                        else {
                                            return Style.content_main;
                                        }
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                TableViewColumn {
                    id: actionsColumn
                    elideMode: Text.ElideRight
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

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                CustomToolButton {
                                    icon.source: "qrc:/assets/icon-actions.svg"
                                    //% "Actions"
                                    ToolTip.text: qsTrId("general-actions")
                                    onClicked: {
                                        var item = transactionsTable.model.get(styleData.row);
                                        txContextMenu.cancelEnabled = item.isCancelAvailable;
                                        txContextMenu.deleteEnabled = item.isDeleteAvailable;
                                        txContextMenu.txID = item.rawTxID;
                                        txContextMenu.popup();
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
                    property bool cancelEnabled
                    property bool deleteEnabled
                    property var txID

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
            }
        }
    }

    StackView {
        id: walletView
        anchors.fill: parent
        initialItem: walletLayout

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
                walletView.currentItem.defaultFocusItem.forceActiveFocus();
            }
        }
    }

    Component.onCompleted: {
        if (root.toSend) {
            walletView.push(Qt.createComponent("send.qml"));
            root.toSend = false;
        }
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
