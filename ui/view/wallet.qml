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
    
    SFText {
        font.pixelSize: 36
        color: Style.content_main
        //% "Wallet"
        text: qsTrId("wallet-title")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    Component {
        id: wallet_layout
        Item {            
            
            Row{
                anchors.top: parent.top
                anchors.right: parent.right
                spacing: 19

                CustomButton {
                    palette.button: Style.accent_incoming
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-receive-blue.svg"
                    //% "Receive"
                    text: qsTrId("wallet-receive-button")

                    onClicked: {
                        walletView.push(Qt.createComponent("receive.qml"));
                    }
                }

                CustomButton {
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Send"
                    text: qsTrId("general-send")

                    onClicked: {
                        walletView.push(Qt.createComponent("send.qml"));
                    }
                }
            }

            Item {
                y: 97
                height: 206

                anchors.left: parent.left
                anchors.right: parent.right

                RowLayout {

                    id: wide_panels

                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: parent.height

                    spacing: 30

                    AvailablePanel {
                        Layout.maximumWidth: 700
                        Layout.minimumWidth: 350
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        value: viewModel.available
                        onCopyValueText: BeamGlobals.copyToClipboard(value)
                        onOpenExternal : function() {
                            var externalLink = "https://www.beam.mw/#exchanges";
                            Utils.openExternal(externalLink, viewModel, externalLinkConfirmation);
                        }
                    }

                    SecondaryPanel {
                        Layout.minimumWidth: 350
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        //% "In progress"
                        title: qsTrId("wallet-in-progress-title")
                        receiving: viewModel.receiving
                        sending: viewModel.sending
                        maturing: viewModel.maturing

                        onCopyValueText: BeamGlobals.copyToClipboard(value)
                    }
                }
            }

            Item
            {
                y: 320

                anchors.left: parent.left
                anchors.right: parent.right

                SFText {
                    x: 30

                    font {
                        pixelSize: 18
                        styleName: "Bold"; weight: Font.Bold
                    }

                    color: Style.content_main

                    //% "Transactions"
                    text: qsTrId("wallet-transactions-title")
                }

                CustomToolButton {
                    anchors.right: parent.right
                    icon.source: "qrc:/assets/icon-proof.svg"
                    //% "Verify payment"
                    ToolTip.text: qsTrId("wallet-verify-payment")
                    onClicked: {
                        paymentInfoVerifyDialog.model.reset();
                        paymentInfoVerifyDialog.open();
                    }
                }
            }

            CustomTableView {

                id: transactionsView

                anchors.fill: parent
                anchors.topMargin: 394-33
                Layout.bottomMargin: 9

                property int rowHeight: 69

                frameVisible: false
                selectionMode: SelectionMode.NoSelection
                backgroundVisible: false

                sortIndicatorVisible: true
                sortIndicatorColumn: 1
                sortIndicatorOrder: Qt.DescendingOrder

                Binding{
                    target: viewModel
                    property: "sortRole"
                    value: transactionsView.getColumn(transactionsView.sortIndicatorColumn).role
                }

                Binding{
                    target: viewModel
                    property: "sortOrder"
                    value: transactionsView.sortIndicatorOrder
                }

                property int resizableWidth: parent.width - iconColumn.width - actionsColumn.width

                TableViewColumn {
                    id: iconColumn
                    width: 60
                    elideMode: Text.ElideRight
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            SvgImage {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 26 
                                source: "qrc:/assets/beam-circle.svg"
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.dateRole
                    //% "Date | Time"
                    title: qsTrId("wallet-txs-date-time")
                    width: 160 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    resizable: false
                    movable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            SFLabel {
                                font.pixelSize: 14
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 20
                                elide: Text.ElideRight
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
                    role: viewModel.userRole
                    //% "Address"
                    title: qsTrId("general-address")
                    width: 400 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideMiddle
                    resizable: false
                    movable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
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
                    role: viewModel.amountRole
                    //% "Amount"
                    title: qsTrId("general-amount")
                    width: 200 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            property bool income: (styleData.row >= 0) ? viewModel.transactions[styleData.row].income : false
                            SFLabel {
                                anchors.leftMargin: 20
                                anchors.right: parent.right
                                anchors.left: parent.left
                                color: parent.income ? Style.accent_incoming : Style.accent_outgoing
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 24
                                text: (parent.income ? "+ " : "- ") + styleData.value
                                textFormat: Text.StyledText
                                font.styleName: "Light"
                                font.weight: Font.Thin
                                copyMenuEnabled: true
                                onCopyText: BeamGlobals.copyToClipboard(styleData.value)
                            }
                        }
                    }
                }

                TableViewColumn {
                    role: viewModel.statusRole
                    //% "Status"
                    title: qsTrId("general-status")
                    width: 200 * transactionsView.resizableWidth / 960
                    elideMode: Text.ElideRight
                    movable: false
                    resizable: false
                    delegate: Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight
                            clip:true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                spacing: 14

                                SvgImage {
                                    Layout.alignment: Qt.AlignHCenter
                                    sourceSize: Qt.size(20, 20)
                                    source: getIconSource()

                                    function getIconSource() {
                                        if (!!viewModel.transactions[styleData.row]) {
                                            if (viewModel.transactions[styleData.row].isSelfTx()) {
                                                return "qrc:/assets/icon-transfer.svg";
                                            }

                                            return viewModel.transactions[styleData.row].income ? "qrc:/assets/icon-received.svg" : "qrc:/assets/icon-sent.svg";
                                        }
                                        return "qrc:/assets/icon-sent.svg";
                                    }
                                }

                                SFLabel {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    font.pixelSize: 14
                                    font.italic: true
                                    color: getTextColor()
                                    elide: Text.ElideRight
                                    text: txStatusText(styleData.value)
                                    copyMenuEnabled: true
                                    onCopyText: BeamGlobals.copyToClipboard(text)

                                    function getTextColor () {
                                        if (!viewModel.transactions[styleData.row]) {
                                            return Style.content_main;
                                        }

                                        if (viewModel.transactions[styleData.row].inProgress() || viewModel.transactions[styleData.row].isCompleted()) {
                                            if (viewModel.transactions[styleData.row].isSelfTx()) {
                                                return Style.content_main;
                                            }
                                            return viewModel.transactions[styleData.row].income ? Style.accent_incoming : Style.accent_outgoing;
                                        }

                                        return Style.content_main;
                                    }

                                    function txStatusText(value) {
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
                }

                TableViewColumn {
                    id: actionsColumn
                    role: "status"
                    title: ""
                    width: 40
                    movable: false
                    resizable: false
                    delegate: txActions
                }

                model: viewModel.transactions

                Component {
                    id: txActions
                    Item {
                        Item {
                            width: parent.width
                            height: transactionsView.rowHeight

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
                                        txContextMenu.transaction = viewModel.transactions[styleData.row];
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
                    property TxObject transaction
                    Action {
                        //% "Copy address"
                        text: qsTrId("wallet-txs-copy-addr-cm")
                        icon.source: "qrc:/assets/icon-copy.svg"
                        onTriggered: {
                            if (!!txContextMenu.transaction)
                            {
                                BeamGlobals.copyToClipboard(txContextMenu.transaction.user);
                            }
                        }
                    }
                    Action {
                        //% "Cancel"
                        text: qsTrId("general-cancel")
                        onTriggered: {
                           viewModel.cancelTx(txContextMenu.transaction);
                        }
                        enabled: !!txContextMenu.transaction && txContextMenu.transaction.canCancel
                        icon.source: "qrc:/assets/icon-cancel.svg"
                    }
                    Action {
                        //% "Delete"
                        text: qsTrId("general-delete")
                        icon.source: "qrc:/assets/icon-delete.svg"
                        enabled: !!txContextMenu.transaction && txContextMenu.transaction.canDelete
                        onTriggered: {
                            //% "The transaction will be deleted. This operation can not be undone"
                            deleteTransactionDialog.text = qsTrId("wallet-txs-delete-message");
                            deleteTransactionDialog.open();
                        }
                    }
                    Connections {
                        target: deleteTransactionDialog
                        onAccepted: {
                            viewModel.deleteTx(txContextMenu.transaction);
                        }
                    }
                }
                // Transaction details
                rowDelegate: Item {
                    height: transactionsView.rowHeight
                    id: rowItem
                    property bool collapsed: true

                    width: parent.width
                    Rectangle {
                            height: transactionsView.rowHeight
                            width: parent.width
                            color: Style.background_row_even
                            visible: styleData.alternate
                    }

                    Column {
                        id: rowColumn
                        width: parent.width
                        Rectangle {
                            height: transactionsView.rowHeight
                            width: parent.width
                            color: "transparent"
                        }
                        Item {
                            id: txDetails
                            height: 0
                            visible: height > 0
                            width: parent.width
                            clip: true

                            property int maximumHeight: detailsPanel.height

                            onMaximumHeightChanged: {
                                if (!rowItem.collapsed) {
                                    rowItem.height = maximumHeight + transactionsView.rowHeight
                                    txDetails.height = maximumHeight
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: Style.background_details
                            }
                            TransactionDetails {
                                id: detailsPanel
                                width: transactionsView.width
                                model: !!viewModel.transactions[styleData.row] ? viewModel.transactions[styleData.row] : null
                                onOpenExternal : function() {
                                    var url = Style.explorerUrl + "block?kernel_id=" + model.kernelID;
                                    Utils.openExternal(url, viewModel, externalLinkConfirmation);
                                } 
                                onTextCopied: function (text) { BeamGlobals.copyToClipboard(text);}
                                onShowDetails: {
                                    if (model)
                                    {
                                        paymentInfoDialog.model = model.getPaymentInfo();
                                        paymentInfoDialog.open();
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        height: transactionsView.rowHeight
                        width: parent.width

                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: {
                            if (styleData.row === undefined 
                             || styleData.row < 0
                             || styleData.row >= viewModel.transactions.length)
                            {
                                return;
                            }
                            if (mouse.button === Qt.RightButton )
                            {
                                txContextMenu.transaction = viewModel.transactions[styleData.row];
                                txContextMenu.popup();
                            }
                            else if (mouse.button === Qt.LeftButton && !!viewModel.transactions[styleData.row])
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
                            to: transactionsView.rowHeight + txDetails.maximumHeight
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
                            to: transactionsView.rowHeight
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
                        height: transactionsView.rowHeight
                        TableItem {
                            text: styleData.value
                            elide: styleData.elideMode
                        }
                    }
                }
            }
        }
    }

    StackView {
        id: walletView
        anchors.fill: parent
        initialItem: wallet_layout

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
}
