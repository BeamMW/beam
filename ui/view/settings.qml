import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "controls"
import "utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: settingsView
    Layout.fillWidth: true

    property string linkStyle: "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>"
    property bool swapMode:  false

    SettingsViewModel {
        id: viewModel
    }

    OpenExternalLinkConfirmation {
        id: externalLinkConfirmation
    }

    ChangePasswordDialog {
        id: changePasswordDialog        
    }

    ConfirmPasswordDialog {
        id: confirmPasswordDialog
    }

    ConfirmationDialog {
        id: confirmRefreshDialog
        property bool canRefresh: true
        //% "Rescan"
        okButtonText: qsTrId("general-rescan")
        okButtonIconSource: "qrc:/assets/icon-repeat.svg"
        cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
        cancelButtonVisible: true
        width: 460
        height: 243

        contentItem: Item {
            id: confirmationContent
            Column {
                anchors.fill: parent
                spacing: 30
                
                SFText {
                    width: parent.width
                    topPadding: 20
                    font.pixelSize: 18
                    color: Style.content_main
                    horizontalAlignment : Text.AlignHCenter
                    //% "Rescan"
                    text: qsTrId("general-rescan")
                }
                SFText {
                    width: parent.width
                    leftPadding: 20
                    rightPadding: 20
                    font.pixelSize: 14
                    color: Style.content_main
                    wrapMode: Text.Wrap
                    horizontalAlignment : Text.AlignHCenter
                    //: settings tab, confirm rescan dialog message
                    //% "Rescan will sync transaction and UTXO data with the latest information on the blockchain. The process might take long time."
                    text: qsTrId("settings-rescan-confirmation-message")
                }
                SFText {
                    width: parent.width
                    leftPadding: 20
                    rightPadding: 20
                    topPadding: -15
                    font.pixelSize: 14
                    color: Style.content_main
                    wrapMode: Text.Wrap
                    horizontalAlignment : Text.AlignHCenter
                    //: settings tab, confirm rescan dialog additional message
                    //% "Are you sure?"
                    text: qsTrId("settings-rescan-confirmation-message-line-2")
                }
            }
        }

        onAccepted: {
            canRefresh = false;
            viewModel.refreshWallet();
        }
    }

    RowLayout {
        id: mainColumn
        Layout.fillWidth:     true
        Layout.minimumHeight: 40
        Layout.alignment:     Qt.AlignTop

        Title {
            //% "Settings"
            text: qsTrId("settings-title")
        }

        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom | Qt.AlignRight
            bottomPadding: 7
            horizontalAlignment: Text.AlignRight
            font.pixelSize: 14
            color: Style.content_secondary
            //: settings tab, version label
            //% "Version"
            text: qsTrId("settings-version") + ": " + viewModel.version
        }
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    RowLayout {
        Layout.fillWidth:    true
        Layout.bottomMargin: 23

        Item {
            Layout.fillWidth: true
        }

        CustomSwitch {
            id:                mode
            //% "Swap"
            text:              qsTrId("general-swap")
            Layout.alignment:  Qt.AlignRight
            checked:           settingsView.swapMode
        }

        Binding {
            target:   settingsView
            property: "swapMode"
            value:    mode.checked
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.bottomMargin: 10
        clip: true
        visible: swapMode
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        GridLayout {
            id:               swapLayout
            width:            mainColumn.width
            rowSpacing:       20
            columnSpacing:    20
            columns:          2

            Repeater {
                model: viewModel.swapCoinSettingsList
                SwapNodeSettings {
                    id:                  settingsControl
                    Layout.minimumWidth: swapLayout.width / 2 - swapLayout.columnSpacing / 2
                    
                    title:                    modelData.title
                    showSeedDialogTitle:      modelData.showSeedDialogTitle
                    showAddressesDialogTitle: modelData.showAddressesDialogTitle
                    minFeeRate:               modelData.minFeeRate
                    feeRateLabel:             modelData.feeRateLabel
                    canEdit:                  modelData.canEdit
                    isConnected:              modelData.isConnected
                    isNodeConnection:         modelData.isNodeConnection
                    isElectrumConnection:     modelData.isElectrumConnection
                    connectionStatus:         modelData.connectionStatus
                    getAddressesElectrum:     modelData.getAddressesElectrum

                    //
                    // Node
                    //
                    address:             modelData.nodeAddress
                    username:            modelData.nodeUser
                    password:            modelData.nodePass
                    feeRate:             modelData.feeRate

                    //
                    // Electrum
                    //
                    addressElectrum:            modelData.nodeAddressElectrum
                    seedPhrasesElectrum:        modelData.electrumSeedPhrases
                    phrasesSeparatorElectrum:   modelData.phrasesSeparatorElectrum
                    isCurrentElectrumSeedValid: modelData.isCurrentSeedValid

                    Connections {
                        target: modelData
                        onFeeRateChanged:        settingsControl.feeRate = modelData.feeRate
                        onCanEditChanged:        settingsControl.canEdit = modelData.canEdit
                        onConnectionTypeChanged: { 
                            settingsControl.isConnected          = modelData.isConnected;
                            settingsControl.isNodeConnection     = modelData.isNodeConnection;
                            settingsControl.isElectrumConnection = modelData.isElectrumConnection;
                            settingsControl.title                = modelData.title;
                        }
                        onConnectionStatusChanged: {
                            settingsControl.connectionStatus     = modelData.connectionStatus;
                        }

                        //
                        // Node
                        //
                        onNodeAddressChanged: settingsControl.address  = modelData.nodeAddress
                        onNodeUserChanged:    settingsControl.username = modelData.nodeUser
                        onNodePassChanged:    settingsControl.password = modelData.nodePass
                        //
                        // Electrum
                        //
                        onNodeAddressElectrumChanged: settingsControl.addressElectrum = modelData.nodeAddressElectrum
                        onElectrumSeedPhrasesChanged: settingsControl.seedPhrasesElectrum = modelData.electrumSeedPhrases
                        onIsCurrentSeedValidChanged:  settingsControl.isCurrentElectrumSeedValid = modelData.isCurrentSeedValid
                    }

                    onApplyNode:                 modelData.applyNodeSettings()
                    onClearNode:                 modelData.resetNodeSettings()
                    onApplyElectrum:             modelData.applyElectrumSettings()
                    onClearElectrum:             modelData.resetElectrumSettings()
                    onNewSeedElectrum:           modelData.newElectrumSeed()
                    onRestoreSeedElectrum:       modelData.restoreSeedElectrum()
                    onDisconnect:                modelData.disconnect()
                    onConnectToNode:             modelData.connectToNode()
                    onConnectToElectrum:         modelData.connectToElectrum()
                    onCopySeedElectrum:          modelData.copySeedElectrum()
                    onValidateCurrentSeedPhrase: modelData.validateCurrentElectrumSeedPhrase()

                    Binding {
                        target:   modelData
                        property: "nodeAddress"
                        value:    settingsControl.address
                    }

                    Binding {
                        target:   modelData
                        property: "nodeUser"
                        value:    settingsControl.username
                    }

                    Binding {
                        target:   modelData
                        property: "nodePass"
                        value:    settingsControl.password
                    }

                    Binding {
                        target:   modelData
                        property: "feeRate"
                        value:    settingsControl.feeRate
                    }

                    Binding {
                        target:   modelData
                        property: "nodeAddressElectrum"
                        value:    settingsControl.addressElectrum
                    }
                }
            }
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.bottomMargin: 10
        clip: true
        visible: !swapMode
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        RowLayout {
            width: mainColumn.width
            spacing: 10

            ColumnLayout {
                Layout.preferredWidth: settingsView.width * 0.4
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft

                Rectangle {
                    id: nodeBlock
                    Layout.fillWidth: true
                    radius: 10
                    color: Style.background_second
                    Layout.preferredHeight: viewModel.localNodeRun ? 460 : (nodeAddressError.visible ? 285 : 240)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 30
                        spacing: 10

                        SFText {
                            Layout.preferredHeight: 21
                            //: settings tab, node section, title
                            //% "Node"
                            text: qsTrId("settings-node-title")
                            color: Style.content_main
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        RowLayout {
                            Layout.preferredHeight: 16
                            Layout.topMargin: 15

                            CustomSwitch {
                                id: localNodeRun
                                Layout.fillWidth: true
                                //: settings tab, node section, run node label
                                //% "Run local node"
                                text: qsTrId("settings-local-node-run-checkbox")
                                font.pixelSize: 14
                                width: parent.width
                                checked: viewModel.localNodeRun
                                Binding {
                                    target: viewModel
                                    property: "localNodeRun"
                                    value: localNodeRun.checked
                                }
                            }
                        }

                        Item {
                            Layout.preferredHeight: 12
                        }

                        RowLayout {
                            Layout.preferredHeight: 16
                            visible: viewModel.localNodeRun

                            SFText {
                                Layout.fillWidth: true;
                                //: settings tab, node section, port label
                                //% "Port"
                                text: qsTrId("settings-local-node-port")
                                color: Style.content_secondary
                                font.pixelSize: 14
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            SFTextInput {
                                id: localNodePort
                                Layout.preferredWidth: nodeBlock.width * 0.55
                                Layout.alignment: Qt.AlignRight
                                activeFocusOnTab: true
                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.localNodePort
                                validator: IntValidator {
                                    bottom: 1
                                    top: 65535
                                }
                                Binding {
                                    target: viewModel
                                    property: "localNodePort"
                                    value: localNodePort.text
                                }
                            }
                        }

                        RowLayout {
                            Layout.preferredHeight: 16
                            visible: !viewModel.localNodeRun
                            SFText {
                                Layout.fillWidth: true
                                //: settings tab, node section, address label
                                //% "ip:port"
                                text: qsTrId("settings-remote-node-ip-port")
                                color: Style.content_secondary
                                font.pixelSize: 14
                            }

                            SFTextInput {
                                id: nodeAddress
                                Layout.fillWidth: true
                                Layout.maximumWidth: nodeBlock.width * 0.6
                                Layout.minimumWidth: nodeBlock.width * 0.5
                                focus: true
                                activeFocusOnTab: true
                                font.pixelSize: 14
                                color: Style.content_main
                                validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([1-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                text: viewModel.nodeAddress
                                Binding {
                                    target: viewModel
                                    property: "nodeAddress"
                                    value: nodeAddress.text.trim()
                                }
                            }
                        }

                        RowLayout {
                            id: nodeAddressError
                            Layout.preferredHeight: 16
                            visible: !viewModel.localNodeRun && (!viewModel.isValidNodeAddress || !nodeAddress.acceptableInput)

                            Item {
                                Layout.fillWidth: true;
                            }

                            SFText {
                                Layout.preferredWidth: nodeBlock.width * 0.6
                                color: Style.validator_error
                                font.pixelSize: 14
                                font.italic: true
                                //% "Invalid address"
                                text: qsTrId("general-invalid-address")
                            }
                        }

                        SFText {
                            Layout.topMargin: 15
                            //: settings tab, node section, peers label
                            //% "Peers"
                            text: qsTrId("settings-peers-title")
                            color: Style.content_main
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                            visible: viewModel.localNodeRun
                        }

                        RowLayout {
                            Layout.minimumHeight: 25
                            Layout.maximumHeight: 41
                            Layout.preferredHeight: 25
                            spacing: 10
                            visible: viewModel.localNodeRun

                            SFTextInput {
                                Layout.preferredWidth: nodeBlock.width * 0.7
                                id: newLocalNodePeer
                                activeFocusOnTab: true
                                font.pixelSize: 14
                                color: Style.content_main
                                validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([1-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Image {
                                Layout.alignment: Qt.AlignRight
                                Layout.preferredHeight: 16
                                Layout.preferredWidth: 16
                                source: "qrc:/assets/icon-add-green.svg"
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: newLocalNodePeer.acceptableInput ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: {
                                        if (newLocalNodePeer.acceptableInput) {
                                            viewModel.addLocalNodePeer(newLocalNodePeer.text.trim());
                                            newLocalNodePeer.clear();
                                        }
                                    }
                                }
                            }
                        }

                        ListView {
                            visible: viewModel.localNodeRun
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: viewModel.localNodePeers
                            clip: true
                            delegate: RowLayout {
                                width: parent.width
                                height: 36

                                SFText {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelData
                                    font.pixelSize: 14
                                    color: Style.content_main
                                    height: 16
                                    elide: Text.ElideRight
                                }

                                CustomToolButton {
                                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                    Layout.minimumHeight: 20
                                    Layout.minimumWidth: 20
                                    leftPadding: 5
                                    rightPadding: 5
                                    icon.source: "qrc:/assets/icon-delete.svg"
                                    enabled: localNodeRun.checked
                                    onClicked: viewModel.deleteLocalNodePeer(index)
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: 42
                            CustomButton {
                                Layout.preferredHeight: 38
                                Layout.preferredWidth: 125
                                leftPadding: 25
                                rightPadding: 25
                                spacing: 12
                                //% "Cancel"
                                text: qsTrId("general-cancel")
                                icon.source: "qrc:/assets/icon-cancel-white.svg"
                                enabled: {
                                    viewModel.isChanged
                                    && nodeAddress.acceptableInput
                                    && localNodePort.acceptableInput
                                }
                                onClicked: viewModel.undoChanges()
                            }

                            Item {
                                Layout.maximumWidth: 30
                                Layout.fillWidth: true
                            }

                            PrimaryButton {
                                Layout.preferredHeight: 38
                                Layout.preferredWidth: 125
                                leftPadding: 25
                                rightPadding: 25
                                spacing: 12
                                //: settings tab, node section, apply button
                                //% "Apply"
                                text: qsTrId("settings-apply")
                                icon.source: "qrc:/assets/icon-done.svg"
                                enabled: {
                                    viewModel.isChanged
                                    && nodeAddress.acceptableInput
                                    && localNodePort.acceptableInput
                                    && (localNodeRun.checked ? (viewModel.localNodePeers.length > 0) : viewModel.isValidNodeAddress)
                                }
                                onClicked: viewModel.applyChanges()
                            }
                        }
                    }
                }
            }

            Item {
                Layout.preferredWidth: 10
            }

            ColumnLayout {
                Layout.preferredWidth: settingsView.width * 0.6
                Layout.alignment: Qt.AlignTop | Qt.AlignRight

                Rectangle {
                    id: generalBlock
                    Layout.fillWidth: true
                    radius: 10
                    color: Style.background_second
                    Layout.preferredHeight: 330

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 30
                        spacing: 10
                        SFText {
                            Layout.preferredHeight: 21
                            //: settings tab, general section, title
                            //% "General settings"
                            text: qsTrId("settings-general-title")
                            color: Style.content_main
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        RowLayout {
                            Layout.preferredHeight: 16
                            Layout.topMargin: 15

                            ColumnLayout {
                                SFText {
                                    Layout.fillWidth: true
                                    //: settings tab, general section, lock screen label
                                    //% "Lock screen"
                                    text: qsTrId("settings-general-lock-screen")
                                    color: Style.content_secondary
                                    font.pixelSize: 14
                                }
                            }

                            Item {
                            }

                            ColumnLayout {
                                CustomComboBox {
                                    id: lockTimeoutControl
                                    fontPixelSize: 14
                                    Layout.preferredWidth: generalBlock.width * 0.33

                                    currentIndex: viewModel.lockTimeout
                                    model: [
                                        //% "Never"
                                        qsTrId("settings-general-lock-screen-never"),
                                        //% "1 minute"
                                        qsTrId("settings-general-lock-screen-1m"),
                                        //% "5 minutes"
                                        qsTrId("settings-general-lock-screen-5m"),
                                        //% "15 minutes"
                                        qsTrId("settings-general-lock-screen-15m"),
                                        //% "30 minutes"
                                        qsTrId("settings-general-lock-screen-30m"),
                                        //% "1 hour"
                                        qsTrId("settings-general-lock-screen-1h"),
                                    ]
                                    onActivated: {
                                        viewModel.lockTimeout = lockTimeoutControl.currentIndex;
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.preferredHeight: 15
                        }

                        RowLayout {
                            Layout.preferredHeight: 16

                            ColumnLayout {
                                SFText {
                                    Layout.fillWidth: true
                                    //: settings tab, general section, language label
                                    //% "Language"
                                    text: qsTrId("settings-general-language")
                                    color: Style.content_secondary
                                    font.pixelSize: 14
                                }
                            }

                            Item {
                            }

                            ColumnLayout {
                                CustomComboBox {
                                    id: language
                                    Layout.preferredWidth: generalBlock.width * 0.33
                                    fontPixelSize: 14

                                    model: viewModel.supportedLanguages
                                    currentIndex: viewModel.currentLanguageIndex
                                    onActivated: {
                                        viewModel.currentLanguage = currentText;
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.preferredHeight: 10
                        }

                        SFText {
                            //: settings tab, general section, wallet data folder location label
                            //% "Wallet folder location"
                            text: qsTrId("settings-wallet-location-label")
                            color: Style.content_main
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        RowLayout {
                            SFText {
                                Layout.fillWidth: true
                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.walletLocation
                                elide: Text.ElideMiddle
                            }

                            SFText {
                                Layout.fillWidth: false
                                Layout.alignment: Qt.AlignRight
                                font.pixelSize: 14
                                color: Style.active
                                //% "Show in folder"
                                text: qsTrId("general-show-in-folder")
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        viewModel.openFolder(viewModel.walletLocation);
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.preferredHeight: 10
                        }

                        RowLayout {
                            Layout.preferredHeight: 16

                            CustomSwitch {
                                id: isPasswordReqiredToSpendMoney
                                //: settings tab, general section, ask password to send label
                                //% "Ask password for every sending transaction"
                                text: qsTrId("settings-general-require-pwd-to-spend")
                                font.pixelSize: 14
                                Layout.fillWidth: true
                                checked: viewModel.isPasswordReqiredToSpendMoney
                                function onDialogAccepted() {
                                    viewModel.isPasswordReqiredToSpendMoney = checked;
                                }

                                function onDialogRejected() {
                                    checked = !checked;
                                }
                                onClicked: {
                                    //: settings tab, general section, ask password to send, confirm password dialog, title
                                    //% "Don’t ask password on every Send"
                                    confirmPasswordDialog.dialogTitle = qsTrId("settings-general-require-pwd-to-spend-confirm-pwd-title");
                                    //: settings tab, general section, ask password to send, confirm password dialog, message
                                    //% "Password verification is required to change that setting"
                                    confirmPasswordDialog.dialogMessage = qsTrId("settings-general-require-pwd-to-spend-confirm-pwd-message");
                                    confirmPasswordDialog.onDialogAccepted = onDialogAccepted;
                                    confirmPasswordDialog.onDialogRejected = onDialogRejected;
                                    confirmPasswordDialog.open();
                                }
                            }
                        }

                        RowLayout {
                            Layout.preferredHeight: 32

                            SFText {
                                property string beamUrl: "<a href='https://www.beam.mw/'>beam.mw</a>"
                                //% "blockchain explorer"
                                            property string explorerUrl: "<a href='%1'>%2</a>".arg(Style.explorerUrl).arg(qsTrId("explorer"))
                                //: general settings, label for alow open external links
                                //% "Allow access to %1 and %2 (to fetch exchanges and transaction data)"
                                text: Style.linkStyle + qsTrId("settings-general-allow-beammw-label").arg(beamUrl).arg(explorerUrl)
                                textFormat: Text.RichText
                                font.pixelSize: 14
                                color: allowBeamMWLinks.palette.text
                                wrapMode: Text.WordWrap
                                Layout.preferredWidth: generalBlock.width - 95
                                Layout.preferredHeight: 32
                                MouseArea {
                                    id: allowOpenExternalArea
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    onClicked: {
                                        if(!Utils.handleExternalLink(mouse, allowOpenExternalArea, viewModel, externalLinkConfirmation))
                                        {
                                            viewModel.isAllowedBeamMWLinks = !viewModel.isAllowedBeamMWLinks;
                                        }
                                    }
                                    hoverEnabled: true
                                    onPositionChanged : {
                                        Utils.handleMousePointer(mouse, allowOpenExternalArea);
                                    }
                                }
                            }

                            Item {
                                Layout.preferredWidth: 10
                            }

                            CustomSwitch {
                                id: allowBeamMWLinks
                                Layout.preferredWidth: 30
                                checked: viewModel.isAllowedBeamMWLinks
                                Binding {
                                    target: viewModel
                                    property: "isAllowedBeamMWLinks"
                                    value: allowBeamMWLinks.checked
                                }
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    Layout.topMargin: 25

                    CustomButton {
                        Layout.preferredWidth: 250
                        Layout.preferredHeight: 38
                        Layout.alignment: Qt.AlignLeft
                        Layout.leftMargin: 5
                        //% "Change wallet password"
                        text: qsTrId("general-change-pwd")
                        palette.buttonText : "white"
                        palette.button: Style.background_second
                        icon.source: "qrc:/assets/icon-password.svg"
                        icon.width: 16
                        icon.height: 16
                        onClicked: changePasswordDialog.open()
                    }

                    Item {
                        Layout.maximumWidth: 30
                        Layout.fillWidth: true
                    }

                    CustomButton {
                        Layout.preferredWidth: 250
                        Layout.preferredHeight: 38
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 5
                        //% "Rescan"
                        text: qsTrId("general-rescan")
                        palette.button: Style.background_second
                        palette.buttonText : viewModel.localNodeRun ? Style.content_main : Style.content_disabled
                        icon.source: "qrc:/assets/icon-repeat-white.svg"
                        enabled: viewModel.localNodeRun && confirmRefreshDialog.canRefresh && viewModel.isLocalNodeRunning
                        onClicked: {
                            confirmRefreshDialog.open();
                        }
                    }
                }

                Rectangle {
                    id: feedBackBlock
                    Layout.fillWidth: true
                    Layout.topMargin: 25
                    radius: 10
                    color: Style.background_second
                    Layout.preferredHeight: 240

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 30
                        spacing: 20

                        SFText {
                            //: settings tab, report problem section, title
                            //% "Report problem"
                            text: qsTrId("settings-report-problem-title")
                            color: Style.content_main
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }
                        SFText {
                            property string beamEmail: "<a href='mailto:support@beam.mw'>support@beam.mw</a>"
                            property string beamGithub: "<a href='https://github.com/BeamMW'>Github</a>"
                            //% "To report a problem:"
                            property string rpm0: qsTrId("settings-report-problem-message-l0")
                            //% "1. Click “Save wallet logs” and choose a destination folder for log archive"
                            property string rpm1: qsTrId("settings-report-problem-message-l1")
                            //% "2. Send email to %1 or open a ticket in %2"
                            property string rpm2: qsTrId("settings-report-problem-message-l2").arg(beamEmail).arg(beamGithub)
                            //% "3. Don’t forget to attach logs archive"
                            property string rpm3: qsTrId("settings-report-problem-message-l3")
                            Layout.topMargin: 7
                            Layout.preferredWidth: parent.width
                            text: Style.linkStyle + rpm0 + "<br />" + rpm1 + "<br />" + rpm2 + "<br />" + rpm3
                            textFormat: Text.RichText
                            color: Style.content_main
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            MouseArea {
                                id: reportProblemMessageArea
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: {
                                    Utils.handleExternalLink(mouse, reportProblemMessageArea, viewModel, externalLinkConfirmation);
                                }
                                hoverEnabled: true
                                onPositionChanged : {
                                    Utils.handleMousePointer(mouse, reportProblemMessageArea);
                                }
                            }
                        }

                        CustomButton {
                            Layout.topMargin: 10
                            Layout.preferredHeight: 38
                            Layout.preferredWidth: 200
                            Layout.alignment: Qt.AlignLeft
                            //: settings tab, report problem section, save logs button
                            //% "Save wallet logs"
                            text: qsTrId("settings-report-problem-save-log-button")
                            icon.source: "qrc:/assets/icon-save.svg"
                            palette.buttonText : "white"
                            palette.button: Style.background_button
                            onClicked: viewModel.reportProblem()
                        }
                    }
                }
            }
        }
    }
}
