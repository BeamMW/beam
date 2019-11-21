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
        //% "Atomic Swaps are in BETA"
        title: qsTrId("swap-beta-title")
        //% "I understand"
        okButtonText:        qsTrId("swap-alert-confirm-button")
        okButtonIconSource:  "qrc:/assets/icon-done.svg"
        cancelButtonVisible: false
        width: 470
        //% "Atomic Swaps functionality is Beta at the moment. We recommend you not to send large amounts."
        text: qsTrId("swap-beta-message")
    }

    ConfirmationDialog {
        id:                     cancelOfferDialog
        property var txId:      undefined
        width:                  460
        //% "Cancel offer"
        title:                  qsTrId("atomic-swap-cancel")
        //% "Are you sure you want to cancel your offer?"
        text:                   qsTrId("atomic-swap-cancel-text")
        //% "cancel offer"
        okButtonText:           qsTrId("atomic-swap-cancel-button")
        okButtonIconSource:     "qrc:/assets/icon-cancel-black.svg"
        okButtonColor:          Style.swapCurrencyStateIndicator
        //% "back"
        cancelButtonText:       qsTrId("atomic-swap-back-button")
        cancelButtonIconSource: "qrc:/assets/icon-back.svg"
        onAccepted: {
            viewModel.cancelOffer(cancelOfferDialog.txId);
        }
        Connections {
            target: viewModel
            onOfferRemovedFromTable: function(txId) {
                if (cancelOfferDialog.txId == txId) {
                    cancelOfferDialog.cancelButton.onClicked();
                }
            }
        }
    }

    ConfirmationDialog {
        id:                     cancelSwapDialog
        property var txId:      undefined
        //% "Cancel atomic swap"
        title:                  qsTrId("atomic-swap-tx-cancel")
        //% "Are you sure you want to cancel?"
        text:                   qsTrId("atomic-swap-tx-cancel-text")
        //% "yes"
        okButtonText:           qsTrId("atomic-swap-tx-yes-button")
        okButtonIconSource:     "qrc:/assets/icon-done.svg"
        okButtonColor:          Style.swapCurrencyStateIndicator
        //% "no"
        cancelButtonText:       qsTrId("atomic-swap-no-button")
        cancelButtonIconSource: "qrc:/assets/icon-cancel-16.svg"
        onAccepted: {
            viewModel.cancelTx(cancelSwapDialog.txId);
        }
    }

    Component.onCompleted: {
        if (viewModel.showBetaWarning) {
            betaDialog.open()
        }
    }

    RowLayout {
        Title {
            //% "Atomic Swaps"
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

            // callbacks for send views
            function onAccepted() {
                offersStackView.pop();
                atomicSwapLayout.state = "transactions";
                transactionsTab.state = "filterInProgressTransactions";
            }
            function onClosed() {
                offersStackView.pop();
            }
            function onSwapToken(token) {
                tokenDuplicateChecker.checkTokenForDuplicate(token);
            }
            
            TokenDuplicateChecker {
                id: tokenDuplicateChecker
                onAccepted: {
                    offersStackView.pop();
                }
                Connections {
                    target: tokenDuplicateChecker.model
                    onTokenPreviousAccepted: function(token) {
                        tokenDuplicateChecker.isOwn = false;
                        tokenDuplicateChecker.open();
                    }
                    onTokenFirstTimeAccepted: function(token) {
                        offersStackView.pop();
                        offersStackView.push(Qt.createComponent("send_swap.qml"),
                                            {
                                                "onAccepted": atomicSwapLayout.onAccepted,
                                                "onClosed": atomicSwapLayout.onClosed
                                            });
                        offersStackView.currentItem.setToken(token);
                    }
                    onTokenOwnGenerated: function(token) {
                        tokenDuplicateChecker.isOwn = true;
                        tokenDuplicateChecker.open();
                    }
                }
            }

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
                    icon.source: "qrc:/assets/icon-accept-offer.svg"
                    //% "Accept offer"
                    text: qsTrId("atomic-swap-accept")
                    font.pixelSize: 12
                    onClicked: {
                        offersStackView.push(Qt.createComponent("send.qml"),
                                             {
                                                "onClosed":    onClosed,
                                                "onSwapToken": onSwapToken
                                             });
                    }
                }
                
                CustomButton {
                    id: sendOfferButton
                    Layout.minimumWidth: 172
                    Layout.preferredHeight: 32
                    Layout.maximumHeight: 32
                    palette.button: Style.accent_incoming
                    palette.buttonText: Style.content_opposite
                    icon.source: "qrc:/assets/icon-create-offer.svg"
                    //% "Create offer"
                    text: qsTrId("atomic-swap-create")
                    font.pixelSize: 12
                    //font.capitalization: Font.AllUppercase

                    onClicked: {
                        function onClosed() {offersStackView.pop();}
                        offersStackView.push(Qt.createComponent("receive_swap.qml"), {"onClosed": onClosed});
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
                    amount: viewModel.beamAvailable
                    currencySymbol: Utils.symbolBeam
                    valueSecondaryStr: activeTxCountStr()
                    visible: true
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
                    amount: viewModel.hasBtcTx ? "" : viewModel.btcAvailable
                    currencySymbol: Utils.symbolBtc
                    valueSecondaryStr: parent.btcActiveTxStr()
                    isOk: viewModel.btcOK
                    isConnecting: viewModel.btcConnecting
                    visible: BeamGlobals.haveBtc()
                    //% "Connecting..."
                    textConnecting: qsTrId("swap-connecting")
                    //% "Cannot connect to peer. Please check the address and retry."
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftLTC
                    currencyIcon: "qrc:/assets/icon-ltc.svg"
                    amount: viewModel.hasLtcTx ? "" : viewModel.ltcAvailable
                    currencySymbol: Utils.symbolLtc
                    valueSecondaryStr: parent.ltcActiveTxStr()
                    isOk: viewModel.ltcOK
                    isConnecting: viewModel.ltcConnecting
                    visible: BeamGlobals.haveLtc()
                    textConnecting: qsTrId("swap-connecting")
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    gradLeft: Style.swapCurrencyPaneGrLeftQTUM
                    currencyIcon: "qrc:/assets/icon-qtum.svg"
                    amount: viewModel.hasQtumTx ? "" : viewModel.qtumAvailable
                    currencySymbol: Utils.symbolQtum
                    valueSecondaryStr: parent.qtumActiveTxStr()
                    isOk: viewModel.qtumOK
                    isConnecting: viewModel.qtumConnecting
                    visible: BeamGlobals.haveQtum()
                    textConnecting: qsTrId("swap-connecting")
                    textConnectionError: qsTrId("swap-beta-connection-error")
                }

                SwapCurrencyAmountPane {
                    id: swapOptions
                    gradLeft: Style.swapCurrencyPaneGrLeftOther
                    gradRight: Style.swapCurrencyPaneGrLeftOther
                    //% "Connect other currency wallet to start trading"
                    amount: qsTrId("atomic-swap-connect-other")
                    amountWrapMode: Text.Wrap
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
                    showLed: false
                    font {
                        pixelSize: 14
                        letterSpacing: 4
                        capitalization: Font.AllUppercase
                    }
                }

                TxFilter {
                    id: transactionsTabSelector
                    Layout.alignment: Qt.AlignTop
                    Layout.leftMargin: 40
                    //% "Transactions"
                    label: qsTrId("atomic-swap-transactions-tab")
                    onClicked: {
                        atomicSwapLayout.state = "transactions";
                        transactionsTab.state = "filterAllTransactions"
                    }
                    showLed: false
                    font {
                        pixelSize: 14
                        letterSpacing: 4
                        capitalization: Font.AllUppercase
                    }
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
                            color: sendReceiveBeamSwitch.checked
                                ? Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)
                                : Style.active
                            //% "Receive BEAM"
                            text: qsTrId("atomic-swap-receive-beam")
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: {
                                    sendReceiveBeamSwitch.checked = !sendReceiveBeamSwitch.checked;
                                }
                            }
                        }

                        CustomSwitch {
                            id: sendReceiveBeamSwitch
                            alwaysGreen: true
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
                        }

                        SFText {
                            Layout.alignment:  Qt.AlignHCenter | Qt.AlignLeft
                            Layout.leftMargin: 10
                            font.pixelSize: 14
                            color: sendReceiveBeamSwitch.checked
                                ? Style.active
                                : Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)
                            //% "Send BEAM"
                            text: qsTrId("atomic-swap-send-beam")
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: {
                                    sendReceiveBeamSwitch.checked = !sendReceiveBeamSwitch.checked;
                                }
                            }
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
                            visible: false
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
                            id: coinSelector
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignRight
                            height: 32
                            Layout.minimumWidth: 70
                            Layout.maximumWidth: 80

                            fontPixelSize: 14
                            fontLetterSpacing: 0.47
                            color: Style.content_main
                            model: ["BTC", "LTC", "QTUM"]
                        }                        
                        Binding {
                            target:   viewModel
                            property: "selectedCoin"
                            value:    coinSelector.currentIndex
                        }
                    }   // RowLayout

                    ColumnLayout {
                        Layout.minimumWidth: parent.width
                        Layout.minimumHeight: parent.height
                        visible: offersTable.model.count == 0

                        SvgImage {
                            Layout.topMargin: 100
                            Layout.alignment: Qt.AlignHCenter
                            source:     "qrc:/assets/atomic-empty-state.svg"
                            sourceSize: Qt.size(60, 60)
                        }

                        SFText {
                            Layout.topMargin:     30
                            Layout.alignment:     Qt.AlignHCenter
                            horizontalAlignment:  Text.AlignHCenter
                            font.pixelSize:       14
                            color:                Style.content_main
                            opacity:              0.5
                            lineHeight:           1.43
/*% "There are no active offers at the moment.
Please try again later or create an offer yourself."
*/
                            text:                 qsTrId("atomic-no-offers")
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }

                    CustomTableView {
                        id: offersTable

                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth : true
                        Layout.fillHeight : true
                        Layout.topMargin: 14
                        visible: offersTable.model.count > 0

                        property int rowHeight: 56
                        property int columnWidth: (width - swapCoinsColumn.width) / 6

                        frameVisible: false
                        selectionMode: SelectionMode.NoSelection
                        backgroundVisible: false
                        sortIndicatorVisible: true
                        sortIndicatorColumn: 1
                        sortIndicatorOrder: Qt.DescendingOrder

                        onSortIndicatorColumnChanged: {
                            sortIndicatorOrder = sortIndicatorColumn == 1 || sortIndicatorColumn == 5
                                ? Qt.DescendingOrder
                                : Qt.AscendingOrder;
                        }

                        model: SortFilterProxyModel {
                            id: proxyModel
                            source: SortFilterProxyModel {
                                source: SortFilterProxyModel {
                                    // filter all offers by selected coin
                                    source: viewModel.allOffers                                
                                    filterRole: "swapCoin"
                                    filterString: getCoinName(viewModel.selectedCoin)
                                    filterSyntax: SortFilterProxyModel.Wildcard
                                    filterCaseSensitivity: Qt.CaseInsensitive
                                }
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
                                height: offersTable.rowHeight
                                property var swapCoin: styleData.value
                                property var isSendBeam: offersTable.model.getRoleValue(styleData.row, "isBeamSide")
                                property var isOwnOffer: offersTable.model.getRoleValue(styleData.row, "isOwnOffer")
                                
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20
                                anchors.topMargin: 18

                                RowLayout {
                                    layoutDirection: Qt.RightToLeft
                                    spacing: -4
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam
                                            ? "qrc:/assets/icon-beam.svg"
                                            : getCoinIcon(swapCoin)
                                    }
                                    SvgImage {
                                        sourceSize: Qt.size(20, 20)
                                        source: isSendBeam
                                            ? getCoinIcon(swapCoin)
                                            : "qrc:/assets/icon-beam.svg"
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
                                fontWeight: Font.Bold
                                fontStyleName: "Bold"
                                fontSizeMode: Text.Fit
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
                                fontWeight: Font.Bold
                                fontStyleName: "Bold"
                                fontSizeMode: Text.Fit
                            }
                        }

                        TableViewColumn {
                            role: "rate"
                            //% "Rate"
                            title: qsTrId("atomic-swap-rate")
                            width: offersTable.columnWidth
                            movable: false
                            resizable: false
                            delegate: TableItem {
                                text: Utils.number2Locale(styleData.value)
                            }
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
                                    property var isOwnOffer: offersTable.model.getRoleValue(styleData.row, "isOwnOffer")

                                    SFText {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: 20

                                        font.pixelSize: 14
                                        color: isOwnOffer ? Style.swapCurrencyStateIndicator : Style.active
                                        text: isOwnOffer
                                                        //% "Cancel offer"
                                                        ? qsTrId("atomic-swap-cancel")
                                                        //% "Accept offer"
                                                        : qsTrId("atomic-swap-accept")

                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (isOwnOffer) {
                                                    cancelOfferDialog.txId = offersTable.model.getRoleValue(styleData.row, "rawTxID");
                                                    cancelOfferDialog.open();
                                                }
                                                else {
                                                    var txParameters = offersTable.model.getRoleValue(styleData.row, "rawTxParameters");
                                                    var token = BeamGlobals.rawTxParametrsToTokenStr(txParameters);
                                                    tokenDuplicateChecker.checkTokenForDuplicate(token);
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
                    anchors.topMargin: 20

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

                        //CustomToolButton {
                        //    Layout.alignment: Qt.AlignRight
                        //    rightPadding: 5
                        //    icon.source: "qrc:/assets/icon-delete.svg"
                        //}
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
                            PropertyChanges { target: txProxyModel; filterString: "true" }
                        }
                    ]

                    CustomTableView {
                        id: transactionsTable

                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 14

                        property int rowHeight: 56
                        property int columnWidth: (width - txSwapCoinsColumn.width - txSwapActionColumn.width) / 6

                        frameVisible: false
                        selectionMode: SelectionMode.NoSelection
                        backgroundVisible: false
                        sortIndicatorVisible: true
                        sortIndicatorColumn: 1
                        sortIndicatorOrder: Qt.DescendingOrder

                        onSortIndicatorColumnChanged: {
                            sortIndicatorOrder = sortIndicatorColumn == 1 
                                ? Qt.DescendingOrder
                                : Qt.AscendingOrder;
                        }

                        model: SortFilterProxyModel {
                            id: txProxyModel
                            source: viewModel.transactions

                            sortOrder: transactionsTable.sortIndicatorOrder
                            sortCaseSensitivity: Qt.CaseInsensitive
                            sortRole: transactionsTable.getColumn(transactionsTable.sortIndicatorColumn).role + "Sort"

                            filterRole: "isInProgress"
                            // filterString: "*"
                            filterSyntax: SortFilterProxyModel.Wildcard
                            filterCaseSensitivity: Qt.CaseInsensitive
                        }

                        rowDelegate: Item {
                            id: rowItem
                            height: ctransactionsTable.rowHeight
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
                                    height: rowItem.height
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
                                    SwapTransactionDetails {
                                        id: detailsPanel
                                        width: transactionsTable.width

                                        property var txRolesMap: myModel
                                        txId:                           txRolesMap && txRolesMap.txID ? txRolesMap.txID : ""
                                        fee:                            txRolesMap && txRolesMap.fee ? txRolesMap.fee : ""
                                        feeRate:                        txRolesMap && txRolesMap.feeRate ? txRolesMap.feeRate : ""
                                        comment:                        txRolesMap && txRolesMap.comment ? txRolesMap.comment : ""
                                        swapCoinName:                   txRolesMap && txRolesMap.swapCoin ? txRolesMap.swapCoin : ""
                                        isBeamSide:                     txRolesMap && txRolesMap.isBeamSideSwap ? txRolesMap.isBeamSideSwap : false
                                        isLockTxProofReceived:          txRolesMap && txRolesMap.isLockTxProofReceived ? txRolesMap.isLockTxProofReceived : false
                                        isRefundTxProofReceived:        txRolesMap && txRolesMap.isRefundTxProofReceived ? txRolesMap.isRefundTxProofReceived : false
                                        swapCoinLockTxId:               txRolesMap && txRolesMap.swapCoinLockTxId ? txRolesMap.swapCoinLockTxId : ""
                                        swapCoinLockTxConfirmations:    txRolesMap && txRolesMap.swapCoinLockTxConfirmations ? txRolesMap.swapCoinLockTxConfirmations : ""
                                        swapCoinRedeemTxId:             txRolesMap && txRolesMap.swapCoinRedeemTxId ? txRolesMap.swapCoinRedeemTxId : ""
                                        swapCoinRedeemTxConfirmations:  txRolesMap && txRolesMap.swapCoinRedeemTxConfirmations ? txRolesMap.swapCoinRedeemTxConfirmations : ""
                                        swapCoinRefundTxId:             txRolesMap && txRolesMap.swapCoinRefundTxId ? txRolesMap.swapCoinRefundTxId : ""
                                        swapCoinRefundTxConfirmations:  txRolesMap && txRolesMap.swapCoinRefundTxConfirmations ? txRolesMap.swapCoinRefundTxConfirmations : ""
                                        beamLockTxKernelId:             txRolesMap && txRolesMap.beamLockTxKernelId ? txRolesMap.beamLockTxKernelId : ""
                                        beamRedeemTxKernelId:           txRolesMap && txRolesMap.beamRedeemTxKernelId ? txRolesMap.beamRedeemTxKernelId : ""
                                        beamRefundTxKernelId:           txRolesMap && txRolesMap.beamRefundTxKernelId ? txRolesMap.beamRefundTxKernelId : ""
                                        stateDetails:                   txRolesMap && txRolesMap.swapState ? txRolesMap.swapState : ""
                                        failureReason:                  txRolesMap && txRolesMap.failureReason ? txRolesMap.failureReason : ""
                                        
                                        onTextCopied: function (text) {
                                            BeamGlobals.copyToClipboard(text);
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
                                property var isSendBeam: transactionsTable.model.getRoleValue(styleData.row, "isBeamSideSwap")
                                
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
                            role: "amountSendWithCurrency"
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
                                        fontStyleName: "Bold"
                                        fontSizeMode: Text.Fit
                                        color: Style.accent_outgoing
                                        onCopyText: BeamGlobals.copyToClipboard(!!model ? model.amountSend  : "")
                                    }
                                }
                            }
                        }
                        TableViewColumn {
                            role: "amountReceiveWithCurrency"
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
                                        fontStyleName: "Bold"
                                        fontSizeMode: Text.Fit
                                        color: Style.accent_incoming
                                        onCopyText: BeamGlobals.copyToClipboard(!!model ? model.amountReceive  : "") 
                                    }
                                }
                            }
                        }
                        TableViewColumn {
                            id: txStatusColumn
                            role: "status"
                            //% "Status"
                            title: qsTrId("atomic-swap-tx-table-status")
                            width: transactionsTable.getAdjustedColumnWidth(txStatusColumn)
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
                                        anchors.leftMargin: 20
                                        spacing: 10

                                        SvgImage {
                                            id: statusIcon
                                            Layout.alignment: Qt.AlignLeft

                                            sourceSize: Qt.size(20, 20)
                                            source: getIconSource(styleData.value)
                                        }
                                        SFLabel {
                                            Layout.alignment: Qt.AlignLeft
                                            Layout.fillWidth: true
                                            font.pixelSize: 14
                                            font.italic: true
                                            wrapMode: Text.WordWrap
                                            text: getStatusText(styleData.value)
                                            verticalAlignment: Text.AlignBottom
                                            color: getTextColor(styleData.value)
                                        }
                                    }
                                }
                            }
                        }
                        TableViewColumn {
                            id: txSwapActionColumn
                            elideMode: Text.ElideRight
                            width: 40
                            movable: false
                            resizable: false
                            delegate: txActions
                        }

                        function showContextMenu(row) {
                            txContextMenu.canCopyToken = transactionsTable.model.getRoleValue(row, "isPending");;
                            txContextMenu.token = transactionsTable.model.getRoleValue(row, "token");
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
                    }   // CustomTableView : transactionsTable

                    ContextMenu {
                        id: txContextMenu
                        modal: true
                        dim: false
                        property bool canCopyToken
                        property bool cancelEnabled
                        property bool deleteEnabled
                        property var txID
                        property string token

                        Action {
                            //% "Copy token"
                            text: qsTrId("swap-copy-token")
                            icon.source: "qrc:/assets/icon-copy.svg"
                            enabled: txContextMenu.canCopyToken
                            onTriggered: {
                                BeamGlobals.copyToClipboard(txContextMenu.token);
                            }
                        }

                        Action {
                            //% "Cancel"
                            text: qsTrId("general-cancel")
                            icon.source: "qrc:/assets/icon-cancel.svg"
                            enabled: txContextMenu.cancelEnabled
                            onTriggered: {
                                cancelSwapDialog.txId = txContextMenu.txID;
                                cancelSwapDialog.open();
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
    
    function getCoinName(idx) {
        switch(idx) {
            case 0: return "btc";
            case 1: return "ltc";
            case 2: return "qtum";
            default: return "";
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

    function getTextColor(status) {
        switch(status)
        {
            case "pending":
            case "in progress":
            case "completed":
                return Style.accent_swap;
            case "failed":
                return Style.accent_fail;
            default:
                return Style.content_secondary;
        }
    }

    function getIconSource(status) {
        switch(status)
        {
            case "pending":
            case "in progress":
                return "qrc:/assets/icon-swap-in-progress.svg";
            case "completed":
                return "qrc:/assets/icon-swap-completed.svg";
            case "failed":
                return "qrc:/assets/icon-swap-failed.svg";
            case "canceled":
                return "qrc:/assets/icon-swap-canceled.svg";
            case "expired":
                return "qrc:/assets/icon-expired.svg";
            default: return "";
        }
    }

    function getStatusText(value) {

        switch(value) {
            //% "waiting for peer"
            case "pending": return qsTrId("wallet-txs-status-waiting-peer");
            //% "in progress"
            case "in progress": return qsTrId("wallet-txs-status-in-progress");
            //% "completed"
            case "completed": return qsTrId("wallet-txs-status-completed");
            //% "cancelled"
            case "canceled": return qsTrId("wallet-txs-status-cancelled");
            //% "expired"
            case "expired": return qsTrId("wallet-txs-status-expired");
            //% "failed"
            case "failed": return qsTrId("wallet-txs-status-failed");
            //% "unknown"
            default: return qsTrId("wallet-txs-status-unknown");
        }
    }
}
