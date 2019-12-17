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
    state: "general"
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
        title: qsTrId("general-rescan")
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

    ConfirmationDialog {
        id: showOwnerKeyDialog
        property string pwd: ""
        //: settings tab, show owner key dialog title
        //% "Owner key"
        title: qsTrId("settings-show-owner-key-title")
        okButtonText: qsTrId("general-copy")
        okButtonIconSource: "qrc:/assets/icon-copy-blue.svg"
        cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
        cancelButtonText: qsTrId("general-close")
        cancelButtonVisible: true
        width: 460

        contentItem: Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: 20
                SFLabel {
                    id: ownerKeyValue
                    Layout.fillWidth: true
                    leftPadding: 20
                    rightPadding: 20
                    topPadding: 15
                    font.pixelSize: 14
                    color: Style.content_secondary
                    wrapMode: Text.WrapAnywhere
                    horizontalAlignment : Text.AlignHCenter
                    text: ""
                    copyMenuEnabled: true
                    onCopyText: BeamGlobals.copyToClipboard(text)
                }
                SFText {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    width: parent.width
                    leftPadding: 20
                    rightPadding: 20
                    bottomPadding: 30
                    font.pixelSize: 14
                    font.italic:    true
                    color: Style.content_main
                    wrapMode: Text.Wrap
                    horizontalAlignment : Text.AlignHCenter
                    //: settings tab, show owner key message
/*% "Please notice, that knowing your owner key allows to
know all your funds (UTXO). Make sure that you
deploy the key at the node you trust completely."*/
                    text: qsTrId("settings-show-owner-key-message")
                }
            }
        }

        onAccepted: {
            BeamGlobals.copyToClipboard(ownerKeyValue.text);
        }

        onOpened: {
            ownerKeyValue.text = viewModel.getOwnerKey(showOwnerKeyDialog.pwd);
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
        Layout.topMargin:    42
        Layout.bottomMargin: 10

        TxFilter {
            id: generalSettingsTab
            Layout.alignment: Qt.AlignVCenter
            //% "General"
            label: qsTrId("general-tab")
            onClicked: settingsView.state = "general"
            capitalization: Font.AllUppercase
        }

        TxFilter {
            id: swapSettingsTab
            Layout.alignment: Qt.AlignVCenter
            //% "Swap"
            label: qsTrId("general-swap")
            onClicked: settingsView.state = "swap"
            capitalization: Font.AllUppercase
        }
    }

    states: [
        State {
            name: "general"
            PropertyChanges { target: generalSettingsTab; state: "active" }
            PropertyChanges { target: settingsView; swapMode: false }
        },
        State {
            name: "swap"
            PropertyChanges { target: swapSettingsTab; state: "active" }
            PropertyChanges { target: settingsView; swapMode: true }
        }
    ]

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
                    feeRateLabel:             modelData.feeRateLabel
                    canEdit:                  modelData.canEdit
                    isConnected:              modelData.isConnected
                    isNodeConnection:         modelData.isNodeConnection
                    isElectrumConnection:     modelData.isElectrumConnection
                    connectionStatus:         modelData.connectionStatus
                    connectionErrorMsg:       modelData.connectionErrorMsg 
                    getAddressesElectrum:     modelData.getAddressesElectrum

                    //
                    // Node
                    //
                    address:             modelData.nodeAddress
                    port:                modelData.nodePort
                    username:            modelData.nodeUser
                    password:            modelData.nodePass
                    feeRate:             modelData.feeRate

                    //
                    // Electrum
                    //
                    addressElectrum:                     modelData.nodeAddressElectrum
                    portElectrum:                        modelData.nodePortElectrum
                    isSelectServerAutomatcally:          modelData.selectServerAutomatically
                    seedPhrasesElectrum:                 modelData.electrumSeedPhrases
                    phrasesSeparatorElectrum:            modelData.phrasesSeparatorElectrum
                    isCurrentElectrumSeedValid:          modelData.isCurrentSeedValid
                    isCurrentElectrumSeedSegwitAndValid: modelData.isCurrentSeedSegwit

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

                        onConnectionErrorMsgChanged: {
                            settingsControl.connectionErrorMsg   = modelData.connectionErrorMsg;
                        }

                        //
                        // Node
                        //
                        onNodeAddressChanged: settingsControl.address  = modelData.nodeAddress
                        onNodePortChanged:    settingsControl.port     = modelData.nodePort
                        onNodeUserChanged:    settingsControl.username = modelData.nodeUser
                        onNodePassChanged:    settingsControl.password = modelData.nodePass
                        //
                        // Electrum
                        //
                        onNodeAddressElectrumChanged: settingsControl.addressElectrum = modelData.nodeAddressElectrum
                        onNodePortElectrumChanged: settingsControl.portElectrum = modelData.nodePortElectrum
                        onSelectServerAutomaticallyChanged: settingsControl.isSelectServerAutomatcally = modelData.selectServerAutomatically
                        onElectrumSeedPhrasesChanged: settingsControl.seedPhrasesElectrum = modelData.electrumSeedPhrases
                        onIsCurrentSeedValidChanged:  settingsControl.isCurrentElectrumSeedValid = modelData.isCurrentSeedValid
                        onIsCurrentSeedSegwitChanged: settingsControl.isCurrentElectrumSeedSegwitAndValid = modelData.isCurrentSeedSegwit
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
                        property: "nodePort"
                        value:    settingsControl.port
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

                    Binding {
                        target:   modelData
                        property: "nodePortElectrum"
                        value:    settingsControl.portElectrum
                    }

                    Binding {
                        target:   modelData
                        property: "selectServerAutomatically"
                        value:    settingsControl.isSelectServerAutomatcally
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
                    Layout.preferredHeight: viewModel.localNodeRun ? 460 : (nodeAddressError.visible ? 330 : 285)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 30
                        spacing: 10

                        SFText {
                            //: settings tab, node section, title
                            //% "Node"
                            text: qsTrId("settings-node-title")
                            color: Style.content_main
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        CustomSwitch {
                            id: localNodeRun
                            Layout.fillWidth: true
                            Layout.topMargin: 15
                            Layout.bottomMargin: 24
                            //: settings tab, node section, run node label
                            //% "Run local node"
                            text: qsTrId("settings-local-node-run-checkbox")
                            font.pixelSize: 14
                            checked: viewModel.localNodeRun
                            Binding {
                                target: viewModel
                                property: "localNodeRun"
                                value: localNodeRun.checked
                            }
                        }

                        RowLayout {
                            visible: viewModel.localNodeRun

                            SFText {
                                Layout.fillWidth: true;
                                Layout.preferredWidth: 3
                                //: settings tab, node section, port label
                                //% "Port"
                                text: qsTrId("settings-local-node-port")
                                color: Style.content_secondary
                                font.pixelSize: 14
                            }


                            SFTextInput {
                                id: localNodePort
                                Layout.fillWidth: true;
                                Layout.preferredWidth: 7
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

                        GridLayout {
                            Layout.fillWidth: true
                            visible: !viewModel.localNodeRun
                            columns : 2
                            SFText {
                                Layout.fillWidth: true
                                Layout.preferredWidth:3
                                //: settings tab, node section, address label
                                //% "Remote node address"
                                text: qsTrId("settings-remote-node-address")
                                color: Style.content_secondary
                                font.pixelSize: 14
                                wrapMode: Text.WordWrap
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 7
                                Layout.alignment: Qt.AlignTop
                                spacing: 0

                                SFTextInput {
                                    id: nodeAddress
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    topPadding: 0
                                    focus: true
                                    activeFocusOnTab: true
                                    font.pixelSize: 14
                                    color:  text.length && (!viewModel.isValidNodeAddress || !nodeAddress.acceptableInput) ? Style.validator_error : Style.content_main
                                    backgroundColor:  text.length && (!viewModel.isValidNodeAddress || !nodeAddress.acceptableInput) ? Style.validator_error : Style.content_main
                                    validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(\s|\x180E)*$/ }
                                    text: viewModel.nodeAddress
                                    Binding {
                                        target: viewModel
                                        property: "nodeAddress"
                                        value: nodeAddress.text.trim()
                                    }
                                }

                                Item {
                                    id: nodeAddressError
                                    Layout.fillWidth: true

                                    SFText {
                                        color:          Style.validator_error
                                        font.pixelSize: 12
                                        font.italic:    true
                                        text:           qsTrId("general-invalid-address")
                                        visible:        (!viewModel.isValidNodeAddress || !nodeAddress.acceptableInput)
                                    }
                                }
                            }

                            // remote port
                            SFText {
                                Layout.fillWidth: true;
                                Layout.preferredWidth: 3
                                text: qsTrId("settings-local-node-port")
                                color: Style.content_secondary
                                font.pixelSize: 14
                            }
                            
                            SFTextInput {
                                id: remoteNodePort
                                Layout.fillWidth: true
                                Layout.preferredWidth: 7
                                Layout.alignment: Qt.AlignRight
                                activeFocusOnTab: true
                                font.pixelSize: 14
                                color: Style.content_main
                                text: viewModel.remoteNodePort
                                validator: IntValidator {
                                    bottom: 1
                                    top: 65535
                                }
                                Binding {
                                    target: viewModel
                                    property: "remoteNodePort"
                                    value: remoteNodePort.text
                                }
                            }
                            Item {
                                Layout.fillHeight: true
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

                 CustomButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 10
                    //: settings tab, general section, Show owner key button and dialog title
                    //% "Show owner key"
                    text: qsTrId("settings-general-require-pwd-to-show-owner-key")
                    palette.button: Style.background_second
                    palette.buttonText : viewModel.localNodeRun ? Style.content_main : Style.content_disabled
                    onClicked: {
                        //: settings tab, general section, Show owner key button and dialog title
                        //% "Show owner key"
                        confirmPasswordDialog.dialogTitle = qsTrId("settings-general-require-pwd-to-show-owner-key");
                        //: settings tab, general section, ask password to Show owner key, message
                        //% "Password verification is required to see the owner key"
                        confirmPasswordDialog.dialogMessage = qsTrId("settings-general-require-pwd-to-show-owner-key-message");
                         //: settings tab, general section, Show owner key button and dialog title
                         //% "Show owner key"
                        confirmPasswordDialog.okButtonText = qsTrId("settings-general-require-pwd-to-show-owner-key")
                        confirmPasswordDialog.okButtonIcon = "qrc:/assets/icon-show-key.svg"
                        confirmPasswordDialog.onDialogAccepted = function () {
                            showOwnerKeyDialog.pwd = confirmPasswordDialog.pwd;
                            showOwnerKeyDialog.open();
                        };
                        confirmPasswordDialog.onDialogRejected = function() {}
                        confirmPasswordDialog.open();
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
                           visible: false  // Remove to enable language dropdown
                        }
                        
                        Item {
                           Layout.preferredHeight: 10
                           visible: false  // Remove to enable language dropdown
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
                                    //% "Don't ask password on every Send"
                                    confirmPasswordDialog.dialogTitle = qsTrId("settings-general-require-pwd-to-spend-confirm-pwd-title");
                                    //: settings tab, general section, ask password to send, confirm password dialog, message
                                    //% "Password verification is required to change that setting"
                                    confirmPasswordDialog.dialogMessage = qsTrId("settings-general-require-pwd-to-spend-confirm-pwd-message");
                                    //: confirm password dialog, ok button
				                    //% "Proceed"
                                    confirmPasswordDialog.okButtonText = qsTrId("general-proceed")
                                    confirmPasswordDialog.okButtonIcon = "qrc:/assets/icon-done.svg"
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
                                linkEnabled: true
                                onLinkActivated:  {
                                    Utils.handleExternalLink(link, viewModel, externalLinkConfirmation)
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
                            linkEnabled: true
                            onLinkActivated: {
                                Utils.handleExternalLink(link, viewModel, externalLinkConfirmation);
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
