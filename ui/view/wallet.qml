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

    function onAccepted() { walletStackView.pop(); }
    function onClosed() { walletStackView.pop(); }
    function onSwapToken(token) {
        tokenDuplicateChecker.checkTokenForDuplicate(token);
    }

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

    TokenDuplicateChecker {
        id: tokenDuplicateChecker
        Connections {
            target: tokenDuplicateChecker.model
            onTokenPreviousAccepted: function(token) {
                tokenDuplicateChecker.isOwn = false;
                tokenDuplicateChecker.open();
            }
            onTokenFirstTimeAccepted: function(token) {
                walletStackView.pop();
                walletStackView.push(Qt.createComponent("send_swap.qml"),
                                     {
                                         "onAccepted": onAccepted,
                                         "onClosed": onClosed
                                     });
                walletStackView.currentItem.setToken(token);
            }
            onTokenOwnGenerated: function(token) {
                tokenDuplicateChecker.isOwn = true;
                tokenDuplicateChecker.open();
            }
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
                    id: sendButton
                    height: 32
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Send"
                    text: qsTrId("general-send")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        walletStackView.push(Qt.createComponent("send.qml"),
                                             {
                                                 "isSwapMode": false,
                                                 "onClosed": onClosed,
                                                 "onSwapToken": onSwapToken,
                                                 "onAddress": onAddress
                                             });

                        function onAddress(token) {
                            walletStackView.pop();
                            walletStackView.push(Qt.createComponent("send_regular.qml"),
                                                {"onAccepted": onAccepted,
                                                 "onClosed": onClosed,
                                                 "onSwapToken": onSwapToken});
                            walletStackView.currentItem.setToken(token);
                        }
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
                        walletStackView.push(Qt.createComponent("receive_regular.qml"),
                                            {"onClosed": onClosed,
                                             "onSwapMode": onSwapMode});
                        function onSwapMode() {
                            walletStackView.pop();
                            walletStackView.push(Qt.createComponent("receive_swap.qml"),
                                                {"onClosed": onClosed,
                                                 "onRegularMode": onRegularMode});
                        }
                        function onRegularMode() {
                            walletStackView.pop();
                            walletStackView.push(Qt.createComponent("receive_regular.qml"),
                                                {"onClosed": onClosed,
                                                 "onSwapMode": onSwapMode});
                        }
                    }
                }
            }

            AvailablePanel {
                Layout.topMargin: 32
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.maximumHeight: 67
                Layout.minimumHeight: 67

                width: parseFloat(viewModel.beamSending) > 0 || parseFloat(viewModel.beamReceiving) > 0 ? parent.width : (parent.width / 2)

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
                Layout.preferredHeight: 32
                Layout.bottomMargin: 10

                TxFilter {
                    id: allTabSelector
                    Layout.alignment: Qt.AlignVCenter
                    //% "All"
                    label: qsTrId("wallet-transactions-all-tab")
                    onClicked: transactionsLayout.state = "all"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: inProgressTabSelector
                    Layout.alignment: Qt.AlignVCenter
                    //% "In progress"
                    label: qsTrId("wallet-transactions-in-progress-tab")
                    onClicked: transactionsLayout.state = "inProgress"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: sentTabSelector
                    Layout.alignment: Qt.AlignVCenter
                    //% "Sent"
                    label: qsTrId("wallet-transactions-sent-tab")
                    onClicked: transactionsLayout.state = "sent"
                    capitalization: Font.AllUppercase
                }
                TxFilter {
                    id: receivedTabSelector
                    Layout.alignment: Qt.AlignVCenter
                    //% "Received"
                    label: qsTrId("wallet-transactions-received-tab")
                    onClicked: transactionsLayout.state = "received"
                    capitalization: Font.AllUppercase
                }
                Item {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                }
                SearchBox {
                    id: searchBox
                    Layout.preferredWidth: 400
                    Layout.alignment: Qt.AlignVCenter
                    //% "Transaction or kernel ID, comment, address or contact"
                    placeholderText: qsTrId("wallet-search-transactions-placeholder")
                }
                CustomToolButton {
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
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
                    source: SortFilterProxyModel {
                        
                        source: viewModel.transactions
                        filterRole: "search"
                        filterString: "*" + searchBox.text + "*"
                        filterSyntax: SortFilterProxyModel.Wildcard
                        filterCaseSensitivity: Qt.CaseInsensitive
                    }

                    sortOrder: transactionsTable.sortIndicatorOrder
                    sortCaseSensitivity: Qt.CaseInsensitive
                    sortRole: transactionsTable.getColumn(transactionsTable.sortIndicatorColumn).role + "Sort"

                    filterSyntax: SortFilterProxyModel.Wildcard
                    filterCaseSensitivity: Qt.CaseInsensitive
                }

                rowDelegate: Item {
                    id: rowItem
                    height: transactionsTable.rowHeight
                    anchors.left: parent.left
                    anchors.right: parent.right
                    property bool collapsed: true

                    property var myModel: parent.model

                    onMyModelChanged: {
                        collapsed = true;
                        height = Qt.binding(function(){ return transactionsTable.rowHeight;});
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: styleData.selected ? Style.row_selected :
                                (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
                    }

                    ColumnLayout {
                        id: rowColumn
                        width: parent.width
                        Rectangle {
                            height: 56
                            width: parent.width
                            color: "transparent"
                        }
                        Item {
                            id: txDetails
                            height: 0
                            width: parent.width
                            clip: true

                            property int maximumHeight: detailsPanel.implicitHeight

                            Rectangle {
                                anchors.fill: parent
                                color: Style.background_details
                            }
                            TransactionDetails {
                                id: detailsPanel
                                width: transactionsTable.width

                                property var txRolesMap: myModel
                                sendAddress:        txRolesMap && txRolesMap.addressFrom ? txRolesMap.addressFrom : ""
                                receiveAddress:     txRolesMap && txRolesMap.addressTo ? txRolesMap.addressTo : ""
                                fee:                txRolesMap && txRolesMap.fee ? txRolesMap.fee : ""
                                comment:            txRolesMap && txRolesMap.comment ? txRolesMap.comment : ""
                                txID:               txRolesMap && txRolesMap.txID ? txRolesMap.txID : ""
                                kernelID:           txRolesMap && txRolesMap.kernelID ? txRolesMap.kernelID : ""
                                status:             txRolesMap && txRolesMap.status ? txRolesMap.status : ""
                                failureReason:      txRolesMap && txRolesMap.failureReason ? txRolesMap.failureReason : ""
                                isIncome:           txRolesMap && txRolesMap.isIncome ? txRolesMap.isIncome : false
                                hasPaymentProof:    txRolesMap && txRolesMap.hasPaymentProof ? txRolesMap.hasPaymentProof : false
                                isSelfTx:           txRolesMap && txRolesMap.isSelfTransaction ? txRolesMap.isSelfTransaction : false
                                rawTxID:            txRolesMap && txRolesMap.rawTxID ? txRolesMap.rawTxID : null
                                searchFilter:       searchBox.text
                                hideFiltered:       false

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
                                        if (paymentInfo.paymentProof.length === 0)
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
                                transactionsTable.showContextMenu(styleData.row);
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
                            to: transactionsTable.rowHeight + txDetails.maximumHeight
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
                    width: 200 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                }
                TableViewColumn {
                    role: "addressTo"
                    //% "To"
                    title: qsTrId("general-address-to")
                    elideMode: Text.ElideMiddle
                    width: 200 * transactionsTable.columnResizeRatio
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
                            property var isIncome: !!styleData.value && transactionsTable.model.getRoleValue(styleData.row, "isIncome")
                            TableItem {
                                text: (parent.isIncome ? "+ " : "- ") + styleData.value
                                fontWeight: Font.Bold
                                fontStyleName: "Bold"
                                fontSizeMode: Text.Fit
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
                    width: transactionsTable.getAdjustedColumnWidth(statusColumn)//150 * transactionsTable.columnResizeRatio
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsTable.rowHeight

                            RowLayout {
                                id: statusRow
                                Layout.alignment: Qt.AlignLeft
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                spacing: 10

                                property var isInProgress: transactionsTable.model.getRoleValue(styleData.row, "isInProgress")
                                property var isSelfTransaction: transactionsTable.model.getRoleValue(styleData.row, "isSelfTransaction")
                                property var isIncome: transactionsTable.model.getRoleValue(styleData.row, "isIncome")
                                property var isCompleted: transactionsTable.model.getRoleValue(styleData.row, "isCompleted")
                                property var isExpired: transactionsTable.model.getRoleValue(styleData.row, "isExpired")

                                SvgImage {
                                    id: statusIcon;
                                    Layout.alignment: Qt.AlignLeft
                                    
                                    sourceSize: Qt.size(20, 20)
                                    source: getIconSource()
                                    function getIconSource() {
                                        if (statusRow.isInProgress) {
                                            if (statusRow.isSelfTransaction) {
                                                return "qrc:/assets/icon-sending-own.svg";
                                            }
                                            return statusRow.isIncome ? "qrc:/assets/icon-receiving.svg"
                                                                 : "qrc:/assets/icon-sending.svg";
                                        }
                                        else if (statusRow.isCompleted) {
                                            if (statusRow.isSelfTransaction) {
                                                return "qrc:/assets/icon-sent-own.svg";
                                            }
                                            return statusRow.isIncome ? "qrc:/assets/icon-received.svg"
                                                                 : "qrc:/assets/icon-sent.svg";
                                        }
                                        else if (statusRow.isExpired) {
                                            return "qrc:/assets/icon-failed.svg" 
                                        }
                                        else {
                                            return statusRow.isIncome ? "qrc:/assets/icon-receive-canceled.svg"
                                                                 : "qrc:/assets/icon-send-canceled.svg";
                                        }
                                    }
                                }
                                SFLabel {
                                    Layout.alignment: Qt.AlignLeft
                                    Layout.fillWidth: true
                                    font.pixelSize: 14
                                    font.italic: true
                                    wrapMode: Text.WordWrap
                                    text: getStatusText(styleData.value)
                                    verticalAlignment: Text.AlignBottom
                                    color: getTextColor()
                                    function getTextColor () {
                                        if (statusRow.isInProgress || statusRow.isCompleted) {
                                            if (statusRow.isSelfTransaction) {
                                                return Style.content_main;
                                            }
                                            return statusRow.isIncome ? Style.accent_incoming : Style.accent_outgoing;
                                        }
                                        else {
                                            return Style.content_secondary;
                                        }
                                    }
                                    onTextChanged: {
                                        color = getTextColor();
                                        statusIcon.source = statusIcon.getIconSource();
                                    }
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

                function showContextMenu(row) {
                    txContextMenu.cancelEnabled = transactionsTable.model.getRoleValue(row, "isCancelAvailable");
                    txContextMenu.deleteEnabled = transactionsTable.model.getRoleValue(row, "isDeleteAvailable");
                    txContextMenu.txID = transactionsTable.model.getRoleValue(row, "rawTxID");
                    txContextMenu.popup();
                }

                Component {
                    id: txActions
                    Item {
                        Item {
                            width: parent.width
                            height: transactionsTable.rowHeight
                            CustomToolButton {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                icon.source: "qrc:/assets/icon-actions.svg"
                                //% "Actions"
                                ToolTip.text: qsTrId("general-actions")
                                onClicked: {
                                    transactionsTable.showContextMenu(styleData.row);
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
        id: walletStackView
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
                walletStackView.currentItem.defaultFocusItem.forceActiveFocus();
            }
        }
    }

    Component.onCompleted: {
        if (root.toSend) {
            sendButton.clicked();
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
            //% "in progress"
            case "receiving": return qsTrId("wallet-txs-status-in-progress");
            //% "in progress"
            case "sending": return qsTrId("wallet-txs-status-in-progress");
            //% "sent to own address"
            case "completed": return qsTrId("wallet-txs-status-own-sent");
            //% "sending to own address"
            case "self sending": return qsTrId("wallet-txs-status-own-sending");
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
