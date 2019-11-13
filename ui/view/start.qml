import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0
import QtQuick.Layouts 1.3

Item
{
    id: root

    anchors.fill: parent
    property bool isLockedMode: false
    property bool isBadPortMode: false

    StartViewModel { id: viewModel }

    function migrateWalletDB(path) {
        // copy wallet.db                         
        viewModel.migrateWalletDB(path);
        viewModel.isRecoveryMode = false;
        startWizzardView.push(open, {
            "firstButtonVisible": true,
            //% "Back"
            "firstButtonText": qsTrId("general-back"), 
            "firstButtonIcon": "qrc:/assets/icon-back.svg",
            "firstButtonAction": function() {
                // remove wallet.db file
                viewModel.deleteCurrentWalletDB();
                startWizzardView.pop();
            }
        });
    }
    
    ConfirmationDialog {
        id: restoreWalletConfirmation

        //% "I agree"
        okButtonText: qsTrId("start-restore-confirm-button")
        okButtonIconSource: "qrc:/assets/icon-done.svg"
        okButtonAllLowercase: false
        cancelButtonVisible: false
        width: 460
        height: contentItem.implicitHeight + footer.implicitHeight
        padding: 0

        contentItem: Column {
            width: parent.width
            height: restoreWalletConfirmationTitle.implicitHeight + restoreWalletConfirmationMessage.implicitHeight
            SFText {
                id: restoreWalletConfirmationTitle
                topPadding: 20
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Qt.AlignHCenter
                //% "Restore wallet"
                text: qsTrId("general-restore-wallet")
                color: Style.content_main
                font.pixelSize: 18
                font.styleName: "Bold"
                font.weight: Font.Bold
            }

            SFText {
                id: restoreWalletConfirmationMessage
                padding: 30
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment : Text.AlignHCenter
                width: parent.width
                //% "You are trying to restore an existing Beam Wallet. Please notice that if you use your wallet on another device, your balance will be up to date, but  transaction history and addresses will be kept separately on each device."
                text: qsTrId("start-restore-message-line")
                color: Style.content_main
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
        }
        onAccepted: {
            onClicked: {
                viewModel.isRecoveryMode = true;
                startWizzardView.push(restoreWallet);
            }
        }
    }

    ConfirmationDialog {
        id: seedPhraseSubmitAllert

        //% "I understand"
        okButtonText: qsTrId("restore-finish-alert-button")
        okButtonIconSource: "qrc:/assets/icon-done.svg"
        cancelButtonVisible: false
        width: 460
        height: contentItem.implicitHeight + footer.implicitHeight + 60
        padding: 0

        contentItem: Column {
            width: parent.width
            height: seedPhraseSubmitAllertTitle.implicitHeight + seedPhraseSubmitAllertMessage.implicitHeight
            SFText {
                id: seedPhraseSubmitAllertTitle
                topPadding: 30
                anchors.horizontalCenter: parent.horizontalCenter
                width: 400
                height: 42
                horizontalAlignment: Qt.AlignHCenter
                //% "Do not simultaneously run two wallets initiated from the same seed phrase"
                text: qsTrId("restore-finish-alert-title")
                color: Style.content_main
                font.pixelSize: 18
                font.styleName: "Bold"
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }

            Item {
                height: 30
                width: parent.width
            }

            SFText {
                id: seedPhraseSubmitAllertMessage
                padding: 30
                bottomPadding: 0
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment : Text.AlignHCenter
                width: parent.width
                height: 32
                //% "Don’t use same seed phrase on several devices, your balance and transaction list won’t be synchronized."
                text: qsTrId("restore-finish-alert-message-line")
                color: Style.content_main
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
        }
        onAccepted: {
            onClicked: {
                viewModel.isRecoveryMode = true;
                startWizzardView.push(create);
            }
        }
    }

    StackView {
        id: startWizzardView
        anchors.fill: parent
        focus: true
        onCurrentItemChanged: {
            if (currentItem && currentItem.defaultFocusItem) {
                startWizzardView.currentItem.defaultFocusItem.forceActiveFocus();
            }
        }

        Component {
            id: start
            Rectangle
            {
                color: Style.background_main

                Image {
                    fillMode: Image.PreserveAspectCrop
                    anchors.fill: parent
                    source: "qrc:/assets/bg.svg"
                }

                property Item defaultFocusItem: createNewWallet

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.preferredHeight: Utils.getLogoTopGapSize(parent.height)
                    }

                    LogoComponent {
                        id: logoComponent
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            
                            Item {
                                Layout.fillHeight: true
                                Layout.minimumHeight: 40
                                Layout.maximumHeight: 180
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillWidth: true

                                CustomButton {
                                    text: qsTrId("general-back")
                                    icon.source: "qrc:/assets/icon-back.svg"
                                    Layout.preferredHeight: 38
                                    visible: startWizzardView.depth > 1
                                    onClicked: {
                                        startWizzardView.pop();
                                    }
                                }

                                Item {
                                    Layout.preferredWidth: 30
                                    visible: startWizzardView.depth > 1
                                }

                                ColumnLayout {
                                    spacing: 20

                                    PrimaryButton {
                                        id: createNewWallet
                                        //% "Create new wallet"
                                        text: qsTrId("general-create-wallet")
                                        Layout.preferredHeight: 38
                                        Layout.alignment: Qt.AlignHCenter
                                        icon.source: "qrc:/assets/icon-add-blue.svg"
                                        onClicked: 
                                        {
                                            viewModel.isRecoveryMode = false;
                                            startWizzardView.push(createWalletEntry);
                                        }
                                    }

                                    PrimaryButton {
                                        visible: viewModel.isTrezorEnabled
                                        id: createNewTrezorWallet
                                        //% "Create new Trezor wallet"
                                        text: qsTrId("general-create-trezor-wallet")
                                        Layout.preferredHeight: 38
                                        Layout.alignment: Qt.AlignHCenter
                                        icon.source: "qrc:/assets/icon-add-blue.svg"
                                        onClicked: 
                                        {
                                            viewModel.isRecoveryMode = false;
                                            startWizzardView.push(createTrezorWalletEntry);
                                        }
                                    }                                
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillWidth: true
                                Layout.topMargin: 65
                                spacing: 30
                                SFText {
                                    Layout.alignment: Qt.AlignHCenter
                                    //% "Restore wallet"
                                    text: qsTrId("general-restore-wallet")
                                    color: Style.active
                                    font.pixelSize: 14
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            restoreWalletConfirmation.open();
                                        }
                                        hoverEnabled: true
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                                Layout.minimumHeight: 67
                            }

                            SFText {
                                Layout.alignment:    Qt.AlignHCenter
                                font.pixelSize:      12
                                color:               Qt.rgba(255, 255, 255, 0.3)
                                text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                            }

                            Item {
                                Layout.minimumHeight: 35
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: migrate
            Rectangle
            {
                color: Style.background_main

                Image {
                    fillMode: Image.PreserveAspectCrop
                    anchors.fill: parent
                    source: "qrc:/assets/bg.svg"
                }

                property Item defaultFocusItem: startMigration

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.preferredHeight: Utils.getLogoTopGapSize(parent.height)
                    }

                    LogoComponent {
                        id: logoComponent
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            SFText {
                                Layout.alignment: Qt.AlignHCenter
                                //% "Your wallet will be migrated to v."
                                text: qsTrId("start-migration-message") + viewModel.walletVersion()
                                color: Style.content_main
                                font.pixelSize: 14
                            }

                            Item {
                                Layout.minimumHeight: 30
                                Layout.preferredHeight: 100
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillWidth: true
                                
                                PrimaryButton {
                                    id: startMigration
                                    Layout.preferredHeight: 38
                                    Layout.preferredWidth: 220

                                    //: migration screen, start auto migration button
                                    //% "Start auto migration"
                                    text: qsTrId("start-migration-button")
                                    icon.source: "qrc:/assets/icon-repeat.svg"
                                    onClicked: 
                                    {
                                        startWizzardView.push(selectWalletDBView);
                                    }
                                }

                                Item {
                                    Layout.preferredWidth: 20
                                }

                                CustomButton {
                                    Layout.preferredHeight: 38
                                    Layout.preferredWidth: 320
                                    //: migration screen, select db file button
                                    //% "Select wallet database file manually"
                                    text: qsTrId("start-migration-select-file-button")
                                    icon.source: "qrc:/assets/icon-folder.svg"
                                    onClicked: {
                                        var path = viewModel.selectCustomWalletDB();

                                        if (path.length > 0) {
                                            migrateWalletDB(path);
                                        }
                                    }
                                }
                            }

                            SFText {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 16
                                Layout.topMargin: 65
                                //% "Restore wallet or create a new one"
                                text: qsTrId("general-restore-or-create-wallet")
                                color: Style.active
                                font.pixelSize: 14
                        
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        startWizzardView.push(start);
                                    }
                                    hoverEnabled: true
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                                Layout.minimumHeight: 67
                            }

                            SFText {
                                Layout.alignment:    Qt.AlignHCenter
                                font.pixelSize:      12
                                color:               Qt.rgba(255, 255, 255, 0.3)
                                text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                            }

                            Item {
                                Layout.minimumHeight: 35
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: selectWalletDBView
            Rectangle
            {
                color: Style.background_main
                function next() {
                    if (nextButton.enabled) {
                        nextButton.clicked();
                    }
                }
                Keys.onReturnPressed: {
                    next();
                }
                Keys.onEnterPressed:{
                    next();
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 50

                    SFText {
                        Layout.alignment: Qt.AlignHCenter
                        horizontalAlignment: Qt.AlignHCenter
                        //% "Select the wallet database file"
                        text: qsTrId("general-select-db")
                        color: Style.content_main
                        font.pixelSize: 36
                    }

                    CustomTableView {
                        id: tableView
                        property int rowHeight: 44
                        property int minWidth: 894
                        property int textLeftMargin: 20
                        Layout.alignment: Qt.AlignHCenter 
                        Layout.topMargin: 50
                        Layout.bottomMargin: 9
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: headerHeight + 3*rowHeight
                        Layout.maximumHeight: headerHeight + 5*rowHeight
                        Layout.minimumWidth: minWidth
                        Layout.maximumWidth: minWidth

                        frameVisible: false
                        selectionMode: SelectionMode.SingleSelection
                        backgroundVisible: false
                        model: viewModel.walletDBpaths

                        headerDelegate: Rectangle {
                            height: tableView.headerHeight
                            color: Style.background_second

                            SFLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: tableView.textLeftMargin
                                horizontalAlignment: Qt.AlignHCenter
                                font.pixelSize: tableView.headerTextFontSize
                                color: Style.content_secondary
                                font.weight: Font.Normal
                                text: styleData.value
                            }
                        }

                        TableViewColumn {
                            role: "fullPath"
                            //% "Name"
                            title: qsTrId("start-select-db-thead-name")
                            width: 350
                            movable: false
                            delegate: Item {
                                width: parent.width
                                height: tableView.rowHeight
                                clip:true
                                

                                SFLabel {
                                    id: pathLabel
                                    property bool isPreferred: (viewModel.walletDBpaths && viewModel.walletDBpaths[styleData.row]) ? viewModel.walletDBpaths[styleData.row].isPreferred : false
                                    property string preferredLabelFormat: "<style>span {color: '#00f6d2';}</style><span>%1</span>"
                                    //: start screen, select db for migration, best match label 
                                    //% "(best match)"
                                    property string bestMatchStr: qsTrId("start-select-db-best-match-label")

                                    font.pixelSize: 14
                                    anchors.left: parent.left
                                    anchors.leftMargin: tableView.textLeftMargin
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.RichText 
                                    text: elidedText(styleData.value, isPreferred) + (isPreferred ? " " + preferredLabelFormat.arg(bestMatchStr) : " ")
                                    color: Style.content_main
                                    copyMenuEnabled: true
                                    onCopyText: BeamGlobals.copyToClipboard(text)
                                    Component.onCompleted: {
                                        if (isPreferred) {
                                            tableView.selection.select(styleData.row);
                                            tableView.currentRow = styleData.row;
                                        }
                                    }
                                    function elidedText(str, isPreferred){
                                        var textMetricsTemplate = 'import QtQuick 2.11; TextMetrics{font{family: "SF Pro Display";styleName: "Regular";weight: Font.Normal;pixelSize: 14;}elide: Text.ElideLeft;elideWidth: parent.width - tableView.textLeftMargin;text: "%1"}';
                                        var fullTextStr = isPreferred ? str + " " + pathLabel.bestMatchStr: str;
                                        var textMetrics= Qt.createQmlObject(
                                                textMetricsTemplate.arg(fullTextStr),
                                                pathLabel,
                                                "textMetrics");
                                        var elidedCount = fullTextStr.length - textMetrics.elidedText.length;
                                        return elidedCount ? "…" + str.substr(elidedCount + 3, str.length) : str;
                                    }
                                }
                            }
                        }

                        TableViewColumn {
                            role: "fileSize"
                            //% "Size"
                            title: qsTrId("start-select-db-thead-size")
                            width: 120
                            movable: false
                            delegate: Item {
                                width: parent.width
                                height: tableView.rowHeight
                                clip:true

                                SFLabel {
                                    font.pixelSize: 14
                                    anchors.left: parent.left
                                    anchors.leftMargin: tableView.textLeftMargin
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    //% "kb"
                                    text: Math.round(styleData.value / 1024) + " " + qsTrId("kb-unit")
                                    color: Style.content_main
                                }
                            }
                        }

                        TableViewColumn {
                            role: "creationDateString"
                            //: start screen, select db for migration, Date created column title
                            //% "Date created"
                            title: qsTrId("start-select-db-thead-created")
                            width: 145 
                            movable: false
                        }

                        TableViewColumn {
                            role: "lastWriteDateString"
                            //: start screen, select db for migration, Date modified column title
                            //% "Date modified"
                            title: qsTrId("start-select-db-thead-modified")
                            width: 145 
                            movable: false
                        }

                        TableViewColumn {
                            role: "fullPath"
                            width: 150
                            movable: false
                            delegate: Item {
                                width: parent.width
                                height: tableView.rowHeight
                                clip:true

                                SFLabel {
                                    font.pixelSize: 14
                                    anchors.left: parent.left
                                    anchors.leftMargin: tableView.textLeftMargin
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    //% "Show in folder"
                                    text: qsTrId("general-show-in-folder")
                                    color: Style.active
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            viewModel.openFolder(styleData.value);
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
                                color: styleData.selected ? Style.row_selected :
                                        (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
                            }
                        }

                        itemDelegate: TableItem {
                            elide: Text.ElideRight
                            clip:true

                            SFLabel {
                                font.pixelSize: 14
                                anchors.left: parent.left
                                anchors.leftMargin: tableView.textLeftMargin
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: styleData.value
                                color: Style.content_main
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 64
                    }

                    Row {
                        id: buttons
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 30

                        CustomButton {
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            visible: startWizzardView.depth > 1
                            onClicked: {
                                startWizzardView.pop();
                            }
                        }

                        PrimaryButton {
                            id: nextButton
                            //% "Next"
                            text: qsTrId("general-next")
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            enabled: tableView.currentRow >= 0
                            onClicked: {
                                migrateWalletDB(viewModel.walletDBpaths[tableView.currentRow].fullPath);
                            }
                        }
                    }

                    Item {
                        Layout.minimumHeight: 30
                        Layout.preferredHeight: 100
                    }

                    SFText {
                        Layout.alignment: Qt.AlignHCenter
                        //% "Restore wallet or create a new one"
                        text: qsTrId("general-restore-or-create-wallet")
                        color: Style.active
                        font.pixelSize: 14
                
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                startWizzardView.push(start);
                            }
                            hoverEnabled: true
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 60
                        Layout.maximumHeight: 90
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: createWalletEntry
            Rectangle
            {
                color: Style.background_main
                property Item defaultFocusItem: generateRecoveryPhraseButton

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Create new wallet"
                            text: qsTrId("general-create-wallet")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Create new wallet with generating seed phrase."
                            text: qsTrId("start-create-new-message-line-1")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "If you ever lose your device, you will need this phrase to recover your wallet!"
                            text: qsTrId("start-create-new-message-line-2")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }

                    Row {
                        topPadding: 100
                        spacing: 65
                        Layout.alignment: Qt.AlignHCenter
                        Layout.minimumHeight : 300
                        Layout.maximumHeight: 500
                        SecurityNote{
                            iconSource: "qrc:/assets/eye.svg"
                            //% "Do not let anyone see your seed phrase"
                            text: qsTrId("start-create-new-securiry-note-1")
                        }
                        SecurityNote{
                            iconSource: "qrc:/assets/password.svg"
                            //% "Never type your seed phrase into password managers or elsewhere"
                            text: qsTrId("start-create-new-securiry-note-2")
                        }
                        SecurityNote{
                            iconSource: "qrc:/assets/copy-two-paper-sheets-interface-symbol.svg"
                            //% "Keep the copies of your seed phrase in a safe place"
                            text: qsTrId("start-create-new-securiry-note-3")
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: startWizzardView.pop();
                        }

                        PrimaryButton {
                            id: generateRecoveryPhraseButton
                            //% "Generate seed phrase"
                            text: qsTrId("start-generate-seed-phrase-button")
                            icon.source: "qrc:/assets/icon-recovery.svg"
                            onClicked: startWizzardView.push(generateRecoveryPhrase);
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: createTrezorWalletEntry
            Rectangle
            {
                color: Style.background_main
                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50

                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop

                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Init wallet with Trezor"
                            text: qsTrId("start-init-wallet-with-trezor")
                            color: Style.content_main
                            font.pixelSize: 36
                        }

                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            text: viewModel.isTrezorConnected
                                //% "Found device:"
                                ? qsTrId("start-found-trezor-device") + " " + viewModel.trezorDeviceName
                                //% "There is no device connected, please, connect your hardware wallet."
                                : qsTrId("start-no-trezor-device-connected")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: startWizzardView.pop();
                        }

                        PrimaryButton {
                            id: nextButton
                            enabled: viewModel.isTrezorConnected
                            //% "Next"
                            text: qsTrId("general-next")
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            onClicked: {
                                viewModel.startOwnerKeyImporting();
                                startWizzardView.push(importTrezorOwnerKey);
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: generateRecoveryPhrase
            Rectangle {
                color: Style.background_main
                property Item defaultFocusItem: nextButton

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Create new wallet"
                            text: qsTrId("general-create-wallet")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Your seed phrase is the access key to all the cryptocurrencies in your wallet. Write down the phrase to keep it in a safe or in a locked vault. Without the phrase you will not be able to recover your money."
                            text: qsTrId("start-generate-seed-phrase-message")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }
                    ConfirmationDialog {
                        id: confirRecoveryPhrasesDialog
                        //% "I understand"
                        okButtonText: qsTrId("start-confirm-seed-phrase-button")
                        okButtonIconSource: "qrc:/assets/icon-done.svg"
                        cancelButtonVisible: false
                        width: 460
                        //% "It is strictly recommended to write down the seed phrase on a paper. Storing it in a file makes it prone to cyber attacks and, therefore, less secure."
                        text: qsTrId("start-confirm-seed-phrase-message")
                        onAccepted: {
                            onClicked: startWizzardView.push(checkRecoveryPhrase);
                        }
                    }
                    Grid{
                        id: phrasesView
                        Layout.alignment: Qt.AlignHCenter

                        topPadding: 50
                        columnSpacing: 30
                        rowSpacing:  20

                        Repeater {
                            model:viewModel.recoveryPhrases
                            Rectangle{
                                border.color: Style.background_second
                                color: "transparent"
                                width: 160
                                height: 38
                                radius: 30
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 9
                                    anchors.left: parent.left
                                    color: Style.background_second
                                    width: 20
                                    height: 20
                                    radius: 10
                                    SFText {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.index + 1
                                        font.pixelSize: 10
                                        color: Style.content_main
                                        opacity: 0.5
                                    }
                                }
                                SFText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.phrase
                                    font.pixelSize: 14
                                    color: Style.content_main
                                }
                            }
                        }
                    }
                    
                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 50
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter

                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: {
                                startWizzardView.pop();
                                viewModel.resetPhrases();
                            }
                        }

                        PrimaryButton {
                            id: nextButton
                            //% "Next"
                            text: qsTrId("general-next")
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            onClicked: {confirRecoveryPhrasesDialog.open();}
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: checkRecoveryPhrase
            Rectangle {
                color: Style.background_main
                property Item defaultFocusItem: null

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Create new wallet"
                            text: qsTrId("general-create-wallet")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            horizontalAlignment: Qt.AlignHCenter
                            //% "To ensure the seed phrase is written down, please fill-in the specific words below"
                            text: qsTrId("start-check-seed-phrase-message")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }
 
                    Grid{
                        Layout.alignment: Qt.AlignHCenter

                        topPadding: 50
                        columnSpacing: 30
                        rowSpacing:  20

                        Repeater {
                            model:viewModel.checkPhrases

                            Row {
                                width: 160
                                height: 38
                                spacing: 20
                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 9
                                    width: 20
                                    height: 20
                                    Rectangle {
                                        color: "transparent"
                                        border.color: Style.content_secondary
                                        width: 20
                                        height: 20
                                        radius: 10
                                        SFText {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.index + 1
                                            font.pixelSize: 10
                                            color: Style.content_secondary
                                        }
                                        visible: modelData.value.length == 0
                                    }

                                    Rectangle {
                                        id: correctPhraseRect
                                        color: modelData.isCorrect ? Style.active : Style.validator_error
                                        width: 20
                                        height: 20
                                        radius: 10
                                        SFText {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.index + 1
                                            font.pixelSize: 10
                                            color: Style.background_main
                                        }
                                        visible: modelData.value.length > 0
                                    }

                                    DropShadow {
                                        anchors.fill: correctPhraseRect
                                        radius: 5
                                        samples: 9
                                        color: modelData.isCorrect ? Style.active : Style.validator_error
                                        source: correctPhraseRect
                                        visible: correctPhraseRect.visible
                                    }
                                }

                                SFTextInput {
                                    id: phraseValue
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 6
                                    width: 121
                                    font.pixelSize: 14
                                    color: (modelData.isCorrect || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                    backgroundColor: (modelData.isCorrect || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                    text: modelData.value
                                    Component.onCompleted: {
                                        modelData.value = "";
                                        if (defaultFocusItem == null) {
                                            defaultFocusItem = phraseValue;
                                        }
                                    }
                                }
                                Binding {
                                    target: modelData
                                    property: "value"
                                    value: phraseValue.text
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter

                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: {
                                startWizzardView.pop();
                                viewModel.resetPhrases();
                            }
                        }

                        PrimaryButton {
                            id: checkRecoveryNextButton
                            //% "Next"
                            text: qsTrId("general-next")
                            enabled: {
                                var enable = true;
                                for(var i = 0; i < viewModel.checkPhrases.length; ++i)
                                {
                                    enable &= viewModel.checkPhrases[i].isCorrect;
                                }
                                return enable;
                            }
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            onClicked: startWizzardView.push(create);
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: importTrezorOwnerKey
            Rectangle {
                color: Style.background_main
                property Item defaultFocusItem: null

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Import Trezor Owner Key"
                            text: qsTrId("start-import-trezor-owner-key")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            horizontalAlignment: Qt.AlignHCenter
                            text: viewModel.isOwnerKeyImported 
                                //% "Owner Key imported. Please, enter the password you saw on device to decrypt your Owner Key."
                                ? qsTrId("start-owner-key-imported")
                                //% "Please, look at your Trezor to complete actions..."
                                : qsTrId("start-look-at-trezor-to-complete-actions")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }

                        SFTextInput {
                            id:trezorPassword
                            width: 400
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: viewModel.isOwnerKeyImported
                            font.pixelSize: 14
                            color: Style.content_main
                            echoMode: TextInput.Password
                        }
                    }


                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter

                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            enabled: viewModel.isOwnerKeyImported
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: startWizzardView.pop();
                        }

                        PrimaryButton {
                            id: checkRecoveryNextButton
                            //% "Next"
                            text: qsTrId("general-next")
                            enabled: viewModel.isOwnerKeyImported && viewModel.isPasswordValid(trezorPassword.text)
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            onClicked: {
                                viewModel.setOwnerKeyPassword(trezorPassword.text)
                                startWizzardView.push(create)
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: restoreWallet
            Rectangle {
                color: Style.background_main
                property Item defaultFocusItem: null

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Restore wallet"
                            text: qsTrId("general-restore-wallet")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Type in or paste your seed phrase"
                            text: qsTrId("start-restore-message")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }
 
                    Grid{
                        Layout.alignment: Qt.AlignHCenter

                        topPadding: 50
                        columnSpacing: 30
                        rowSpacing:  20

                        Repeater {
                            model:viewModel.recoveryPhrases

                            Row {
                                width: 160
                                height: 38
                                spacing: 20
                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 9
                                    width: 20
                                    height: 20
                                    Rectangle {
                                        color: "transparent"
                                        border.color: Style.background_second
                                        width: 20
                                        height: 20
                                        radius: 10
                                        SFText {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.index + 1
                                            font.pixelSize: 10
                                            color: Style.background_second
                                        }
                                        visible: modelData.value.length == 0
                                    }

                                    Rectangle {
                                        id: correctPhraseRect
                                        color: modelData.isAllowed ? Style.background_second : Style.validator_error
                                        width: 20
                                        height: 20
                                        radius: 10
                                        SFText {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.index + 1
                                            font.pixelSize: 10
                                            color: Style.content_main
                                            opacity: 0.5
                                        }
                                        visible: modelData.value.length > 0
                                    }
                                }

                                SFTextInput {
                                    id: phraseValue
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 6
                                    width: 121
                                    font.pixelSize: 14
                                    color: (modelData.isAllowed || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                    backgroundColor: (modelData.isAllowed || modelData.value.length == 0) ? Style.content_main : Style.validator_error
                                    text: modelData.value
                                    onTextEdited: {
                                        var phrases = text.split(viewModel.phrasesSeparator);
                                        if (phrases.length > viewModel.recoveryPhrases.length) {
                                            for(var i = 0; i < viewModel.recoveryPhrases.length; ++i)
                                            {
                                                viewModel.recoveryPhrases[i].value = phrases[i];
                                            }
                                        }
                                    }
                                    Component.onCompleted: {
                                        if (modelData.index == 0) {
                                            defaultFocusItem = phraseValue;
                                        }
                                    }
                                }
                                Binding {
                                    target: modelData
                                    property: "value"
                                    value: phraseValue.text
                                }
                            }
                        }
                    }
                    
                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 50
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter

                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: {
                                startWizzardView.pop();
                                viewModel.resetPhrases();
                            }
                        }

                        PrimaryButton {
                            id: checkRecoveryNextButton
                            //% "Next"
                            text: qsTrId("general-next")
                            enabled: {
                                var enable = true;
                                if (viewModel.validateDictionary) {
                                    for(var i = 0; i < viewModel.recoveryPhrases.length; ++i) {
                                        enable &= viewModel.recoveryPhrases[i].isAllowed;
                                    }
                                }
                                return enable;
                            }
                            icon.source: "qrc:/assets/icon-next-blue.svg"
                            onClicked: {
                                viewModel.validateDictionary = true;
                                onClicked: seedPhraseSubmitAllert.open();
                            }
                        }
                    }

                    Keys.onPressed: {
                        if (event.key == Qt.Key_Shift)
                        {
                            viewModel.validateDictionary = false;
                        }
                    }

                    Keys.onReleased: {
                        if (event.key == Qt.Key_Shift)
                        {
                            viewModel.validateDictionary = true;
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: create
            Rectangle
            {
                color: Style.background_main

                property Item defaultFocusItem: password
                property var onEnterPassword: function() {
                    if(password.text.length == 0)
                    {
                        //% "Please, enter password"
                        passwordError.text = qsTrId("general-pwd-empty-error");
                    }
                    else if(password.text != confirmPassword.text)
                    {
                        //% "Passwords do not match"
                        passwordError.text = qsTrId("start-create-pwd-not-match-error");
                        confirmPassword.color = Style.validator_error
                    }
                    else
                    {
                        viewModel.setPassword(password.text);
                        startWizzardView.push(nodeSetup);
                    }
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            text: viewModel.isRecoveryMode
                                //% "Create new password"
                                ? qsTrId("start-recovery-title")
                                //% "Create new wallet"
                                : qsTrId("general-create-wallet")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                        SFText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            horizontalAlignment: Qt.AlignHCenter
                            text: viewModel.isRecoveryMode
                                //% "Create new password to access your wallet"
                                ? qsTrId("start-recovery-pwd-message")
                                //% "Create password to access your wallet"
                                : qsTrId("start-create-pwd-message")
                            color: Style.content_main
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                    }
                    
                    Column {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 400
                        Layout.topMargin: 50
                        spacing: 30

                        Column {
                            width: parent.width
                            spacing: 10

                            SFText {
                                //% "Password"
                                text: qsTrId("start-pwd-label")
                                color: Style.content_main
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFTextInput {
                                id:password
                                width: parent.width
                                font.pixelSize: 14
                                color: Style.content_main
                                echoMode: TextInput.Password
                                onTextChanged: if (password.text.length > 0) passwordError.text = ""
                                onAccepted: {
                                    confirmPassword.forceActiveFocus();
                                }
                            }

                            RowLayout{
                                id: strengthChecker
                                property var strengthTests: 
                                [
                                    //: set passwort, difficulty message, very weak
                                    //% "Very weak password"
                                    {exp: new RegExp("(?=.{1,})")                                                               , color: Style.validator_error, msg: qsTrId("start-pwd-difficulty-very-weak")},
                                    //: set passwort, difficulty message, weak
                                    //% "Weak password"
                                    {exp: new RegExp("((?=.{6,})(?=.*[0-9]))|((?=.{6,})(?=.*[A-Z]))|((?=.{6,})(?=.*[a-z]))")    , color: Style.validator_error, msg: qsTrId("start-pwd-difficulty-weak")},
                                    //: set passwort, difficulty message, medium
                                    //% "Medium strength password"
                                    {exp: new RegExp("((?=.{6,})(?=.*[A-Z])(?=.*[a-z]))|((?=.{6,})(?=.*[0-9])(?=.*[a-z]))")     , color: Style.validator_warning, msg: qsTrId("start-pwd-difficulty-medium")},
                                    //: set passwort, difficulty message, medium
                                    //% "Medium strength password"
                                    {exp: new RegExp("(?=.{8,})(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")                              , color: Style.validator_warning, msg: qsTrId("start-pwd-difficulty-medium")},
                                    //: set passwort, difficulty message, strong
                                    //% "Strong password"
                                    {exp: new RegExp("(?=.{10,})(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")                             , color: Style.active, msg: qsTrId("start-pwd-difficulty-strong")},
                                    //: set passwort, difficulty message, very strong
                                    //% "Very strong password"
                                    {exp: new RegExp("(?=.{10,})(?=.*[!@#\$%\^&\*])(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")          , color: Style.active, msg: qsTrId("start-pwd-difficulty-very-strong")},
                                ]

                                function passwordStrength(pass)
                                {
                                    for(var i = strengthTests.length - 1; i >= 0; i--)
                                        if(strengthTests[i].exp.test(pass))
                                            return i + 1;
                               
                                    return 0;
                                }

                                property var strength: passwordStrength(password.text)
                                width: parent.width
                                spacing: 8

                                Repeater{
                                    model: parent.strengthTests.length

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 4
                                        border.width: index < parent.strength ? 0 : 1
                                        border.color: Style.background_second
                                        radius: 10
                                        color: index < parent.strength ? parent.strengthTests[parent.strength-1].color : Style.background_main
                                    }
                                }
                            }

                            SFText {
                                text: strengthChecker.strength > 0 ? strengthChecker.strengthTests[strengthChecker.strength-1].msg : ""
                                color: Style.content_secondary
                                font.pixelSize: 14
                                height: 16
                                width: parent.width
                            }

                            SFText {
/*% "Strong password needs to meet the following requirements:
•  the length must be at least 10 characters
•  must contain at least one lowercase letter
•  must contain at least one uppercase letter
•  must contain at least one number"
*/
                                text: qsTrId("start-create-pwd-strength-message")
                                color: Style.content_secondary
                                visible: strengthChecker.strength > 0 && strengthChecker.strength < 6
                                font.pixelSize: 14
                                height: 80
                                width: parent.width
                            }
                        }

                        Column {
                            width: parent.width
                            anchors.bottomMargin: 6
                            spacing: 10

                            SFText {
                                //% "Confirm password"
                                text: qsTrId("start-create-pwd-confirm-label")
                                color: Style.content_main
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFTextInput {
                                id: confirmPassword
                                width: parent.width

                                font.pixelSize: 14
                                color: Style.content_main
                                echoMode: TextInput.Password
                                onTextChanged: {
                                    if (confirmPassword.text.length > 0) passwordError.text = ""
                                    this.color = Style.content_main;
                                }
                                onAccepted: {
                                    onEnterPassword();
                                } 
                            }

                            SFText {
                                id: passwordError
                                color: Style.validator_error
                                font.pixelSize: 14
                                height: 16
                                width: parent.width
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 50
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                    
                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            onClicked: startWizzardView.pop();
                        }
                        PrimaryButton {
                            
                            text: viewModel.isRecoveryMode
                                //% "Open my wallet"
                                ? qsTrId("general-open-wallet")
                                //% "Start using your wallet"
                                : qsTrId("general-start-using")
                            icon.source : viewModel.isRecoveryMode
                                ? "qrc:/assets/icon-wallet-small.svg"
                                : "qrc:/assets/icon-next-blue.svg"
                            onClicked: {
                                onEnterPassword();
                            }
                        }
                    }
                     
                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: nodeSetup

            Rectangle
            {   
                id: nodeSetupRectangle
                color: Style.background_main
                property Item defaultFocusItem: localNodeButton

                function onRestoreCancelled(useRandomNode) {
                    if (useRandomNode) {
                        nodeSetupRectangle.defaultFocusItem = randomNodeButton;
                        randomNodeButton.checked = true;
                    } else if (viewModel.getIsRunLocalNode()) {
                        nodeSetupRectangle.defaultFocusItem = localNodeButton;
                        localNodeButton.checked = true;

                        portInput.text = viewModel.localPort;
                        localNodePeer.text = viewModel.localNodePeer;
                    } else {
                        nodeSetupRectangle.defaultFocusItem = remoteNodeButton;
                        remoteNodeButton.checked = true;

                        remoteNodeAddrInput.text = viewModel.remoteNodeAddress;
                    }
                    nodeSetupRectangle.defaultFocusItem.focus = true;
                }

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.fill: parent
                    anchors.topMargin: 50
                    Column {
                        spacing: 30
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 730
                        SFText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Qt.AlignHCenter
                            //% "Setup node connectivity"
                            text: qsTrId("start-node-title")
                            color: Style.content_main
                            font.pixelSize: 36
                        }
                    }

                    Column {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredWidth: 440
                        topPadding: 50

                        clip: true

                        spacing: 15
                        ButtonGroup {
                            id: nodePreferencesGroup
                        }

                        CustomRadioButton {
                            id: localNodeButton
                            //% "Run integrated node (recommended)"
                            text: qsTrId("start-node-integrated-radio")
                            ButtonGroup.group: nodePreferencesGroup
                            font.pixelSize: 14
                            checked: true
                        }
                        Column {
                            id: localNodePanel
                            visible: localNodeButton.checked
                            width: parent.width
                            leftPadding: 34

                            spacing: 10

                            SFText {
                                //% "Enter port to listen"
                                text: qsTrId("start-node-port-label")
                                color: Style.content_main
                                font.pixelSize: 14
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFTextInput {
                                id:portInput
                                width: parent.width

                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.defaultPortToListen()
                                validator: RegExpValidator { regExp: /^\d{1,5}$/ }
                                onTextChanged: if (portInput.text.length > 0) portError.text = ""
                            }
                            SFText {
                                id: portError
                                color: Style.validator_error
                                font.pixelSize: 14
                            }

                            RowLayout {
                                width: parent.width
                                spacing: 10

                                SFText {
                                    //% "Peer"
                                    text: qsTrId("start-node-peer-label")
                                    color: Style.content_main
                                    font.pixelSize: 14
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }

                                SFTextInput {
                                    id: localNodePeer
                                    Layout.fillWidth: true
                                    activeFocusOnTab: true
                                    font.pixelSize: 12
                                    color: Style.content_main
                                    text: viewModel.chooseRandomNode()
                                    validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                    onTextChanged: if (peerError.text.length > 0) peerError.text = ""
                                }
                            }
                            SFText {
                                id: peerError
                                color: Style.validator_error
                                font.pixelSize: 14
                            }
                        }

                        CustomRadioButton {
                            id: randomNodeButton
                            //% "Connect to random remote node"
                            text: qsTrId("start-node-random-radio")
                            ButtonGroup.group: nodePreferencesGroup
                            font.pixelSize: 14
                            enabled: viewModel.isRecoveryMode == false
                        }
                        Row {
                            width: parent.width
                            spacing: 10
                            CustomRadioButton {
                                id: remoteNodeButton
                                //% "Connect to specific remote node"
                                text: qsTrId("start-node-remote-radio")
                                ButtonGroup.group: nodePreferencesGroup
                                font.pixelSize: 14
                                enabled: viewModel.isRecoveryMode == false
                            }
                            SFTextInput {
                                id:remoteNodeAddrInput
                                visible: remoteNodeButton.checked
                                width: parent.width - parent.spacing - remoteNodeButton.width
                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.defaultRemoteNodeAddr()
                                validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                onTextChanged: if (remoteNodeAddrInput.text.length > 0) remoteNodeAddrError.text = ""
                                bottomPadding: 8 // TODO add default value of this item to controls
                            }
                        }
                        Column {
                            id: remoteNodePanel
                            visible: remoteNodeButton.checked
                            width: parent.width
                            leftPadding: 40

                            spacing: 10

                            SFText {
                                id: remoteNodeAddrError
                                color: Style.validator_error
                                font.pixelSize: 14
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 20
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 30

                        CustomButton {
                            //% "Back"
                            text: qsTrId("general-back")
                            icon.source: "qrc:/assets/icon-back.svg"
                            visible: !isBadPortMode
                            onClicked: startWizzardView.pop();
                        }

                        PrimaryButton {
                            text: viewModel.isRecoveryMode ?
                                //% "Restore wallet"
                                qsTrId("general-restore-wallet") :
                                //% "Start using your wallet"
                                qsTrId("general-start-using");
                            icon.source: viewModel.isRecoveryMode ? "qrc:/assets/icon-restore-blue.svg" : "qrc:/assets/icon-next-blue.svg"
                            enabled: nodePreferencesGroup.checkState != Qt.Unchecked
                            onClicked:{
                                if (localNodeButton.checked) {
                                    if (portInput.text.trim().length === 0) {
                                        //% "Please specify port"
                                        portError.text = qsTrId("start-node-port-empty-error");
                                        return;
                                    }
                                    var effectivePort = parseInt(portInput.text.trim());
                                    if (effectivePort > 65535 || effectivePort < 1) {
                                        //% "Port must be a number between 1 and 65535"
                                        portError.text = qsTrId("start-node-port-value-error");
                                        return;
                                    }
                                    if (localNodePeer.text.trim().length === 0) {
                                        //% "Please specify peer"
                                        peerError.text = qsTrId("start-node-peer-empty-error");
                                        return;
                                    }
                                    if (!localNodePeer.acceptableInput) {
                                        //% "Incorrect address"
                                        peerError.text = qsTrId("start-node-peer-error");
                                        return;
                                    }

                                    viewModel.setupLocalNode(parseInt(portInput.text), localNodePeer.text);
                                }
                                else if (remoteNodeButton.checked) {
                                    if (remoteNodeAddrInput.text.trim().length === 0) {
                                        //% "Please specify address of the remote node"
                                        remoteNodeAddrError.text = qsTrId("start-node-empty-error");
                                        return;
                                    }
                                    viewModel.setupRemoteNode(remoteNodeAddrInput.text.trim());
                                }
                                else if (randomNodeButton.checked) {
                                    viewModel.setupRandomNode();
                                }

                                if (isBadPortMode) {
                                    viewModel.onNodeSettingsChanged();
                                    root.parent.setSource("qrc:/loading.qml");
                                } else {
                                    if (viewModel.createWallet()) {
                                        startWizzardView.push("qrc:/loading.qml", {"isRecoveryMode" : viewModel.isRecoveryMode, "isCreating" : true, "cancelCallback": startWizzardView.pop});
                                    }
                                    else {
                                        // TODO(alex.starun): error message if wallet not created
                                    }
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                        Layout.maximumHeight: 143
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Component {
            id: open
            Rectangle
            {
                property Item defaultFocusItem: openPassword

                // default methods for open wallet, can be changed for unlock wallet
                property var openWallet: function (pass) {
                    return viewModel.openWallet(pass);
                }
                property var loadWallet: function () {
                    root.parent.setSource("qrc:/loading.qml", {"isRecoveryMode" : false, "isCreating" : false});
                }
                
                property var checkCapsLockOnActivation: function () {
                    viewModel.checkCapsLock();
                    // OSX hack, to handle capslock shutdonw
                    if (Qt.platform.os == "osx" && viewModel.isCapsLockOn) {
                        var timer = Qt.createQmlObject('import QtQml 2.11; Timer {}', open, "osxCapsTimer");
                        timer.interval = 500;
                        timer.repeat = true;
                        timer.triggered.connect(viewModel.checkCapsLock);
                        timer.start();
                    }
                }
                Component.onCompleted: root.parent.activated.connect(checkCapsLockOnActivation)
                Component.onDestruction: root.parent.activated.disconnect(checkCapsLockOnActivation)

                color: Style.background_main

                Keys.onPressed: {
                    // Linux hack, X11 return caps state with delay
                    if (Qt.platform.os == "linux") {
                        var timer = Qt.createQmlObject('import QtQml 2.11; Timer {}', open, "linuxCapsTimer");
                        timer.interval = 500;
                        timer.repeat = false;
                        timer.triggered.connect(viewModel.checkCapsLock);
                        timer.start();
                    } else {
                        viewModel.checkCapsLock();
                    }
                }
                Keys.onReleased: {
                    // OSX hack, to handle capslock shutdonw
                    if (Qt.platform.os == "osx") {
                        viewModel.checkCapsLock();
                    }
                }

                Image {
                    fillMode: Image.PreserveAspectCrop
                    anchors.fill: parent
                    source: "qrc:/assets/bg.svg"
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.preferredHeight: Utils.getLogoTopGapSize(parent.height)
                    }

                    LogoComponent {
                        id: logoComponent
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            SFText {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 16
                                //% "Enter your password to access the wallet"
                                text: qsTrId("start-open-pwd-invitation")
                                color: Style.content_main
                                font.pixelSize: 14
                            }

                            Item {
                                Layout.preferredHeight: 48
                            }

                            ColumnLayout {
                                Layout.maximumWidth: 400
                                Layout.minimumWidth: 400
                                Layout.preferredHeight: 79
                                Layout.alignment: Qt.AlignHCenter

                                SFText {
                                    //% "Password"
                                    text: qsTrId("start-pwd-label")
                                    color: Style.content_main
                                    font.pixelSize: 14
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }

                                SFTextInput {
                                    id: openPassword
                                    Layout.fillWidth: true
                                    focus: true
                                    activeFocusOnTab: true
                                    font.pixelSize: 14
                                    color: Style.content_main
                                    echoMode: TextInput.Password
                                    onAccepted: btnCurrentWallet.clicked()
                                    onTextChanged: if (openPassword.text.length > 0) openPasswordError.text = ""
                                }

                                SFText {
                                    id: openPasswordError
                                    color: Style.validator_error
                                    font.pixelSize: 14
                                }
                            }

                            Row {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 18
                                Layout.preferredHeight: 38
                                
                                PrimaryButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    id: btnCurrentWallet
                                    //% "Show my wallet"
                                    text: qsTrId("open-show-wallet-button")
                                    icon.source: "qrc:/assets/icon-wallet-small.svg"
                                    onClicked: {
                                        if(openPassword.text.length == 0)
                                        {
                                            //% "Please, enter password"
                                            openPasswordError.text = qsTrId("general-pwd-empty-error");
                                        }
                                        else
                                        {
                                            if(!openWallet(openPassword.text))
                                            {
                                                //% "Invalid password provided"
                                                openPasswordError.text = qsTrId("general-pwd-invalid");
                                            }
                                            else
                                            {
                                                loadWallet();
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillHeight: true
                                Layout.preferredHeight: 36
                                Layout.topMargin: 20
                                Layout.bottomMargin: 9
                                Rectangle {
                                    id: capsWarning
                                    anchors.centerIn: parent
                                    color: Style.caps_warning
                                    width: 152
                                    height: 36
                                    radius: 6
                                    opacity: 0.2
                                    visible: viewModel.isCapsLockOn    
                                }
                                SFText {
                                    anchors.centerIn: capsWarning
                                    horizontalAlignment: Qt.AlignHCenter
                                    verticalAlignment: Qt.AlignVCenter
                                    //% "Caps lock is on!"
                                    text: qsTrId("start-open-caps-warning")
                                    color: Style.content_main
                                    font.pixelSize: 14
                                    visible: viewModel.isCapsLockOn 
                                }
                            }

                            Row {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 16
                                // spacing: 30
                                SFText {
                                    Layout.alignment: Qt.AlignHCenter
                                    //% "Restore wallet or create a new one"
                                    text: qsTrId("general-restore-or-create-wallet")
                                    color: Style.active
                                    font.pixelSize: 14
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            confirmChangeWalletDialog.open();
                                        }
                                        hoverEnabled: true
                                    }
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                                Layout.minimumHeight: 40
                            }

                            SFText {
                                Layout.alignment:    Qt.AlignHCenter
                                font.pixelSize:      12
                                color:               Qt.rgba(255, 255, 255, 0.3)
                                text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                            }

                            Item {
                                Layout.minimumHeight: 35
                            }
                        }
                    }

                    ConfirmationDialog {
                        id: confirmChangeWalletDialog
                        //% "Proceed"
                        okButtonText: qsTrId("general-proceed")
                        okButtonIconSource: "qrc:/assets/icon-done.svg"
                        cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
                        cancelButtonVisible: true
                        width: 460
                        height: 195
                        contentItem: Column {
                            anchors.fill: parent
                            anchors.margins: 30
                            spacing: 20

                            SFText {
                                anchors.horizontalCenter: parent.horizontalCenter
                                horizontalAlignment: Qt.AlignHCenter
                                //% "Restore wallet or create a new one"
                                text: qsTrId("general-restore-or-create-wallet")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"
                                font.weight: Font.Bold
                            }

                            SFText {
                                horizontalAlignment : Text.AlignHCenter
                                width: parent.width
                                //% "If you'll restore a wallet all transaction history and addresses will be lost."
                                text: qsTrId("start-open-change-wallet-message")
                                color: Style.content_main
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                            }
                        }
                        onAccepted: {
                            viewModel.isRecoveryMode = false;
                            startWizzardView.push(start);
                        }
                    }
                }
            }
        }

        Component.onCompleted: {
            if (isBadPortMode) {
                startWizzardView.push(nodeSetup)
            }
            else if (isLockedMode) {
                startWizzardView.push(open, { "openWallet": function (pass) { return viewModel.checkWalletPassword(pass); },
                                              "loadWallet": function () { root.parent.setSource("qrc:/main.qml"); } });
            }
            else if (viewModel.walletExists) {
                startWizzardView.push(open);
            }
            else if (viewModel.isFindExistingWalletDB())
            {
                startWizzardView.push(migrate);
            }
            else {
                startWizzardView.push(start);
            }
        }
    }
}

