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
        Title {
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
            spacing: 0
            state: "offers"

            RowLayout {
                Layout.alignment: Qt.AlignRight | Qt.AlignTop
                Layout.topMargin: 33
                spacing: 20

                CustomButton {
                    id: acceptOfferButton
                    Layout.minimumWidth: 172
                    Layout.preferredHeight: 32
                    Layout.maximumHeight: 32
                    palette.button: Style.accent_outgoing
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-receive-blue.svg"
                    //% "Accept offer"
                    text: qsTrId("atomic-swap-accept")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        offersStackView.push(Qt.createComponent("send.qml"));
                        atomicSwapLayout.state = "transactions";
                        transactionsTab.state = "filterInProgressTransactions";
                    }
                }
                
                CustomButton {
                    id: sendOfferButton
                    Layout.minimumWidth: 172
                    Layout.preferredHeight: 32
                    Layout.maximumHeight: 32
                    palette.button: Style.accent_incoming
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-send-blue.svg"
                    //% "Create offer"
                    text: qsTrId("atomic-swap-create")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        offersStackView.push(Qt.createComponent("receive.qml"), {"isSwapMode": true});
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.fillWidth: true
                Layout.topMargin: 32
                spacing: 10

                SwapCurrencyAmountPane {
                    function activeTxCountStr() {
                        if (viewModel.activeTxCount == 1) {
                            //% "1 active transaction"
                            return qsTrId("atomic-swap-1active-tx-count")
                        }
                        return viewModel.activeTxCount
                            //% "%1 active transactions"
                            ? qsTrId("atomic-swap-active-tx-count")
                                .arg(viewModel.activeTxCount)
                            : "";
                    }
                    gradLeft: Style.swapCurrencyPaneGrLeftBEAM
                    currencyIcon: "qrc:/assets/icon-beam.svg"
                    valueStr: viewModel.beamAvailable + " " + Utils.symbolBeam
                    vatueSecondaryStr: activeTxCountStr()
                    visible: true
                }

                function btcAmount() {
                    return viewModel.hasBtcTx ? "" : viewModel.btcAvailable + " " + Utils.symbolBtc;
                }

                function ltcAmount() {
                    return viewModel.hasLtcTx ? "" : viewModel.ltcAvailable + " " + Utils.symbolLtc;
                }

                function qtumAmount() {
                    return viewModel.hasQtumTx ? "" : viewModel.qtumAvailable + " " + Utils.symbolQtum;
                }

                //% "Transaction is in progress"
                property string kTxInProgress: qsTrId("swap-beta-tx-in-progress")

                function btcActiveTxStr() {
                    return viewModel.hasBtcTx ? kTxInProgress : "";
                }

                function ltcActiveTxStr() {
                    return viewModel.hasLtcTx ? kTxInProgress : "";
                }

                function qtumActiveTxStr() {
                    return viewModel.hasQtumTx ? kTxInProgress : "";
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftBTC
                    currencyIcon: "qrc:/assets/icon-btc.svg"
                    valueStr: parent.btcAmount()
                    vatueSecondaryStr: parent.btcActiveTxStr()
                    showLoader: viewModel.btcOK && parent.btcActiveTxStr().length
                    isOk: viewModel.btcOK
                    visible: BeamGlobals.haveBtc()
                    //% "Cannot connect to peer. Please check in the address in settings and retry."
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftLTC
                    currencyIcon: "qrc:/assets/icon-ltc.svg"
                    valueStr: parent.ltcAmount()
                    vatueSecondaryStr: parent.ltcActiveTxStr()
                    showLoader: viewModel.ltcOK && parent.ltcActiveTxStr().length
                    isOk: viewModel.ltcOK
                    visible: BeamGlobals.haveLtc()
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftQTUM
                    currencyIcon: "qrc:/assets/icon-qtum.svg"
                    valueStr: parent.qtumAmount()
                    vatueSecondaryStr: parent.qtumActiveTxStr()
                    showLoader: viewModel.qtumOK && parent.qtumActiveTxStr().length
                    isOk: viewModel.qtumOK
                    visible: BeamGlobals.haveQtum()
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    id: swapOptions
                    gradLeft: Style.swapCurrencyPaneGrLeftOther
                    gradRight: Style.swapCurrencyPaneGrLeftOther
                    //% "Connect other currency wallet to start trading"
                    valueStr: qsTrId("atomic-swap-connect-other")
                    textSize: 14
                    rectOpacity: 1.0
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
                    //% "Active offers"
                    label: qsTrId("atomic-swap-active-offers-tab")
                    onClicked: atomicSwapLayout.state = "offers"
                    capitalization: Font.AllUppercase
                }

                TxFilter {
                    id: transactionsTabSelector
                    Layout.alignment: Qt.AlignTop
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
            
            Item {
                Layout.fillWidth:  true
                Layout.fillHeight: true

                ColumnLayout {
                    id: activeOffersTab
                    visible: false

                    anchors.fill: parent
                    anchors.topMargin: 20

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
                            id: sendReceiveBeamSwitch
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                            opacity: 0.6
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
                        property int columnWidth: (width - swapCoinsColumn.width) / 6

                        frameVisible: false
                        selectionMode: SelectionMode.NoSelection
                        backgroundVisible: false
                        sortIndicatorVisible: true
                        sortIndicatorColumn: 1
                        sortIndicatorOrder: Qt.DescendingOrder

                        model: SortFilterProxyModel {
                            id: proxyModel
                            source: SortFilterProxyModel {
                                source: viewModel.allOffers

                                sortOrder: offersTable.sortIndicatorOrder
                                sortCaseSensitivity: Qt.CaseInsensitive
                                sortRole: offersTable.getColumn(offersTable.sortIndicatorColumn).role + "Sort"

                                filterRole: "isBeamSide"
                                filterString: sendReceiveBeamSwitch.checked ? "false" : "true"
                                filterSyntax: SortFilterProxyModel.Wildcard
                                filterCaseSensitivity: Qt.CaseInsensitive
                            }

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
                                color: styleData.selected ? Style.row_selected :
                                        (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
                            }
                        }

                        itemDelegate: TableItem {
                            text: styleData.value
                            elide: Text.ElideRight
                        }

                        TableViewColumn {
                            id: swapCoinsColumn
                            role: "swapCoin"
                            width: 55
                            movable: false
                            resizable: false
                            elideMode: Text.ElideRight
                            delegate: Item {
                                id: coinLabels
                                width: parent.width
                                height: transactionsTable.rowHeight
                                property var swapCoin: styleData.value
                                property var isSendBeam: transactionsTable.model.get(styleData.row).isBeamSide
                                
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20
                                anchors.topMargin: 18

                                RowLayout {
                                    layoutDirection: Qt.RightToLeft
                                    spacing: -4
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam ? "qrc:/assets/icon-beam.svg" : getCoinIcon(swapCoin)
                                    }
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam ? getCoinIcon(swapCoin) : "qrc:/assets/icon-beam.svg"
                                    }
                                }
                            }
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
                            id: offerActionsColumn
                            title: ""
                            width: offersTable.getAdjustedColumnWidth(offerActionsColumn)
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
                                        color: isOwnOffer ? Style.content_main : Style.active
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

                    anchors.fill: parent
                    anchors.topMargin: 14

                    state: "filterAllTransactions"

                    RowLayout {

                        TxFilter {
                            id: allTabSelector
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
                            rightPadding: 5
                            textOpacity: 0
                            icon.source: "qrc:/assets/icon-delete.svg"
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
                            PropertyChanges { target: txProxyModel; filterString: "pending" } // "in progress" state should be
                        }
                    ]

                    CustomTableView {
                        id: transactionsTable

                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 12

                        property int rowHeight: 56
                        property int columnWidth: (width - txSwapCoinsColumn.width - 40) / 6

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
                                        sendAddress:        txRolesMap.addressTo ? txRolesMap.addressTo : ""
                                        receiveAddress:     txRolesMap.addressFrom ? txRolesMap.addressFrom : ""
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
                                height: rowItem.height
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
                                        txContextMenu.cancelEnabled = transactionsTable.model.get(styleData.row).isCancelAvailable;
                                        txContextMenu.deleteEnabled = transactionsTable.model.get(styleData.row).isDeleteAvailable;
                                        txContextMenu.txID = transactionsTable.model.get(styleData.row).rawTxID;
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
                                }
                            }
                        }

                        TableViewColumn {
                            id: txSwapCoinsColumn
                            role: "swapCoin"
                            width: 55
                            movable: false
                            resizable: false
                            elideMode: Text.ElideRight
                            delegate: Item {
                                id: coinLabels
                                width: parent.width
                                height: transactionsTable.rowHeight
                                property var swapCoin: styleData.value
                                property var isSendBeam: transactionsTable.model.get(styleData.row).isBeamSideSwap
                                
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20
                                anchors.topMargin: 18

                                RowLayout {
                                    layoutDirection: Qt.RightToLeft
                                    spacing: -4
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam ? getCoinIcon(swapCoin) : "qrc:/assets/icon-beam.svg"
                                    }
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam ? "qrc:/assets/icon-beam.svg" : getCoinIcon(swapCoin)
                                    }
                                }
                            }
                        }

                        TableViewColumn {
                            role: "timeCreated"
                            //% "Created on"
                            title: qsTrId("atomic-swap-tx-table-created")
                            elideMode: Text.ElideRight
                            width: transactionsTable.columnWidth
                            movable: false
                            resizable: false
                        }
                        TableViewColumn {
                            role: "addressFrom"
                            //% "From"
                            title: qsTrId("atomic-swap-tx-table-from")
                            elideMode: Text.ElideMiddle
                            width: transactionsTable.columnWidth
                            movable: false
                            resizable: false
                        }
                        TableViewColumn {
                            role: "addressTo"
                            //% "To"
                            title: qsTrId("atomic-swap-tx-table-to")
                            elideMode: Text.ElideMiddle
                            width: transactionsTable.columnWidth
                            movable: false
                            resizable: false
                        }
                        TableViewColumn {
                            role: "amountSend"
                            //% "Sent"
                            title: qsTrId("atomic-swap-tx-table-sent")
                            elideMode: Text.ElideRight
                            width: transactionsTable.columnWidth
                            movable: false
                            resizable: false
                            delegate: Item {
                                Item {
                                    width: parent.width
                                    height: transactionsTable.rowHeight
                                    TableItem {
                                        text: (styleData.value === '' ? '' : '-') + styleData.value
                                        fontWeight: Font.Bold
                                        color: Style.accent_outgoing
                                        onCopyText: BeamGlobals.copyToClipboard(Utils.getAmountWithoutCurrency(styleData.value)) 
                                    }
                                }
                            }
                        }
                        TableViewColumn {
                            role: "amountReceive"
                            //% "Received"
                            title: qsTrId("atomic-swap-tx-table-received")
                            elideMode: Text.ElideRight
                            width: transactionsTable.columnWidth
                            movable: false
                            resizable: false
                            delegate: Item {
                                Item {
                                    width: parent.width
                                    height: transactionsTable.rowHeight
                                    TableItem {
                                        text: (styleData.value === '' ? '' : '+') + styleData.value
                                        fontWeight: Font.Bold
                                        color: Style.accent_incoming
                                        onCopyText: BeamGlobals.copyToClipboard(Utils.getAmountWithoutCurrency(styleData.value)) 
                                    }
                                }
                            }
                        }
                        TableViewColumn {
                            role: "status"
                            //% "Status"
                            title: qsTrId("atomic-swap-tx-table-status")
                            elideMode: Text.ElideRight
                            width: transactionsTable.columnWidth
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
                                                else return Style.content_main;
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
                            width: transactionsTable.getAdjustedColumnWidth(actionsColumn)
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
                }   // CustomTableView : transactionsTable
            }   // ColumnLayout : transactionsTab
        } // Item : 
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
    
    function getCoinIcon(coin) {
        switch(coin) {
            case "btc": return "qrc:/assets/icon-btc.svg";
            case "ltc": return "qrc:/assets/icon-ltc.svg";
            case "qtum": return "qrc:/assets/icon-qtum.svg";
            default: return "";
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
