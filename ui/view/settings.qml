import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "controls"
import Beam.Wallet 1.0

Rectangle {

    anchors.fill: parent
    color: Style.background_main

    SettingsViewModel {id: viewModel}

    ChangePasswordDialog {
        id: changePasswordDialog        
    }

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        anchors.bottomMargin: 30
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 20
            height: 40

            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                font.pixelSize: 36
                color: Style.content_main
                //% "Settings"
                text: qsTrId("settings-title")
            }

            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom | Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 14
                color: Style.content_main
                //% "Version: "
                text: qsTrId("settings-version") + viewModel.version
            }
        }

            
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            RowLayout {
                id: rowT
                width: mainColumn.width
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        height: 180

                        radius: 10
                        color: Style.background_second

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 5

                            SFText {
                                Layout.alignment: Qt.AlignTop
                                Layout.bottomMargin: 15
                                //% "Remote node"
                                text: qsTrId("settings-remote-node-title")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFText {
                                Layout.alignment: Qt.AlignTop
                                //% "ip:port"
                                text: qsTrId("settings-remote-node-ip-port")
                                color: localNodeRun.checked ? Style.content_disabled : Style.content_main
                                font.pixelSize: 12
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFTextInput {
                                id: nodeAddress
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                focus: true
                                activeFocusOnTab: true
                                font.pixelSize: 12
                                color: readOnly ? Style.content_disabled : Style.content_main
                                readOnly: localNodeRun.checked
                                validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([1-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                text: viewModel.nodeAddress
                                Binding {
                                    target: viewModel
                                    property: "nodeAddress"
                                    value: nodeAddress.text.trim()
                                }
                            }

                            Item {
                                Layout.minimumHeight: 12
                                SFText {
                                    Layout.alignment: Qt.AlignTop
                                    id: nodeAddressError
                                    color: Style.validator_error
                                    font.pixelSize: 10
                                    visible: (!nodeAddress.acceptableInput || !localNodeRun.checked && !viewModel.isValidNodeAddress)
                                    text: "Invalid address"
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        height: 320
                        radius: 10
                        color: Style.background_second

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20

                            SFText {
                                //% "Local node"
                                text: qsTrId("settings-local-node-title")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 30

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 10

                                    CustomSwitch {
                                        id: localNodeRun
                                        //% "Run local node"
                                        text: qsTrId("settings-local-node-run-checkbox")
                                        font.pixelSize: 12
                                        width: parent.width
                                        checked: viewModel.localNodeRun
                                        Binding {
                                            target: viewModel
                                            property: "localNodeRun"
                                            value: localNodeRun.checked
                                        }
                                    }

                                    SFText {
                                        //% "Local node port"
                                        text: qsTrId("settings-local-node-port")
                                        color: localNodeRun.checked ? Style.content_main : Style.content_disabled
                                        font.pixelSize: 12
                                        font.styleName: "Bold"; font.weight: Font.Bold
                                    }

                                    SFTextInput {
                                        id: localNodePort
                                        width: parent.width
                                        activeFocusOnTab: true
                                        font.pixelSize: 12
                                        color: readOnly ? Style.content_disabled : Style.content_main
                                        readOnly: !localNodeRun.checked
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

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 10

                                    SFText {
                                        Layout.topMargin: 5
                                        //% "Peers"
                                        text: qsTrId("settings-local-node-peers")
                                        color: localNodeRun.checked ? Style.content_main : Style.content_disabled
                                        font.pixelSize: 12
                                        font.styleName: "Bold"; font.weight: Font.Bold
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        SFTextInput {
                                            Layout.fillWidth: true
                                            id: newLocalNodePeer
                                            width: parent.width
                                            activeFocusOnTab: true
                                            font.pixelSize: 12
                                            color: readOnly ? Style.content_disabled : Style.content_main
                                            readOnly: !localNodeRun.checked
                                            validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([1-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                        }
                                    
                                        CustomButton {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 70
                                            leftPadding: 20
                                            rightPadding: 20
                                            text: "Add"
                                            palette.button: Style.background_button
                                            palette.buttonText : localNodeRun.checked ? Style.content_main : Style.content_disabled
                                            enabled: newLocalNodePeer.acceptableInput && localNodeRun.checked
                                            onClicked: {
                                                viewModel.addLocalNodePeer(newLocalNodePeer.text.trim());
                                                newLocalNodePeer.clear();
                                            }
                                        }
                                    }

                                    SFText {
                                        //% "Please add at least one peer"
                                        text: qsTrId("settings-local-node-no-peers-error")
                                        color: Style.validator_error
                                        font.pixelSize: 14
                                        fontSizeMode: Text.Fit
                                        minimumPixelSize: 10
                                        font.italic: true
                                        width: parent.width
                                        visible: localNodeRun.checked && !(viewModel.localNodePeers.length > 0)
                                    }

                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.minimumWidth: 140
                                        model: viewModel.localNodePeers
                                        clip: true
                                        delegate: RowLayout {
                                            width: parent.width
                                            height: 36

                                            SFText {
                                                Layout.fillWidth: true
                                                Layout.alignment: Qt.AlignVCenter
                                                text: modelData
                                                font.pixelSize: 12
                                                color: Style.content_main
                                                height: 16
                                                elide: Text.ElideRight
                                            }

                                            CustomButton {
                                                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                                Layout.minimumHeight: 20
                                                Layout.minimumWidth: 20
                                                shadowRadius: 5
                                                shadowSamples: 7
                                                Layout.margins: shadowRadius
                                                leftPadding: 5
                                                rightPadding: 5
                                                textOpacity: 0
                                                icon.source: "qrc:/assets/icon-delete.svg"
                                                enabled: localNodeRun.checked
                                                onClicked: viewModel.deleteLocalNodePeer(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ConfirmationDialog {
                    id: confirmRefreshDialog
                    property bool canRefresh: true
                    //% "rescan"
                    okButtonText: qsTrId("settings-rescan-confirmation-button")
                    okButtonIconSource: "qrc:/assets/icon-repeat.svg"
                    cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"
                    cancelVisible: true
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
                                text: qsTrId("settings-rescan-confirmation-title")
                            }
                            SFText {
                                width: parent.width
                                leftPadding: 20
                                rightPadding: 20
                                font.pixelSize: 14
                                color: Style.content_main
                                wrapMode: Text.Wrap
                                horizontalAlignment : Text.AlignHCenter
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

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 10
                        color: Style.background_second
                        //height: childrenRect.height + 40
                        height: 180

                        Column {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 20
                            spacing: 10

                            Row {
                                SFText {
                                    //% "General settings"
                                    text: qsTrId("settings-general-title")
                                    color: Style.content_main
                                    font.pixelSize: 18
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 10
                                ColumnLayout {
                                    width: parent.width * 2 / 3
                                    SFText {
                                        //% "Language"
                                        text: qsTrId("settings-general-language")
                                        color: Style.content_main
                                        font.pixelSize: 12
                                    }
                                }
                                ColumnLayout {
                                    width: parent.width / 3
                                    CustomComboBox {
                                        id: language
                                        Layout.fillWidth: true
                                        height: 20
                                        Layout.alignment: Qt.AlignRight
                                        anchors.top: parent.top

                                        model: viewModel.supportedLanguages
                                        currentIndex: viewModel.currentLanguageIndex
                                        Binding {
                                            target: viewModel
                                            property: "currentLanguage"
                                            value: language.currentText
                                        }
                                    }
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 10

                                ColumnLayout {
                                    width: parent.width * 2 / 3
                                    SFText {
                                        //% "Lock screen in"
                                        text: qsTrId("settings-general-lock-screen")
                                        color: Style.content_main
                                        font.pixelSize: 12
                                    }
                                }

                                ColumnLayout {
                                    width: parent.width / 3
                                    CustomComboBox {
                                        id: lockTimeoutControl
                                        Layout.fillWidth: true
                                        height: 20
                                        anchors.top: parent.top

                                        currentIndex: viewModel.lockTimeout

                                        Binding {
                                            target: viewModel
                                            property: "lockTimeout"
                                            value: lockTimeoutControl.currentIndex
                                        }

                                        model: [
                                            //% "never"
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
                                    }
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 10

                                CustomSwitch {
                                    id: isPasswordReqiredToSpendMoney
                                    //% "Ask password for every sending transaction"
                                    text: qsTrId("settings-general-require-pwd-to-spend")
                                    font.pixelSize: 12
                                    width: parent.width
                                    checked: viewModel.isPasswordReqiredToSpendMoney
                                    Binding {
                                        target: viewModel
                                        property: "isPasswordReqiredToSpendMoney"
                                        value: isPasswordReqiredToSpendMoney.checked
                                    }
                                }
                            }


                            CustomButton {
                                //% "change wallet password"
                                text: qsTrId("settings-general-change-pwd-button")
                                palette.buttonText : "white"
                                palette.button: Style.background_button
                                icon.source: "qrc:/assets/icon-password.svg"
                                icon.width: 16
                                icon.height: 16
                                onClicked: changePasswordDialog.open()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 320
                        radius: 10
                        color: Style.background_second

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 10

                            SFText {
                                //% "Report problem"
                                text: qsTrId("settings-report-problem-title")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            ColumnLayout {
                                SFText {
                                    Layout.fillWidth: true
                                    //% "To report a problem:"
                                    text: qsTrId("settings-report-problem-title-2")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }

                                SFText {
                                    Layout.fillWidth: true
                                    //% "1. Click 'Save wallet logs' and choose a destination folder for log archive"
                                    text: qsTrId("settings-report-problem-message-line-1")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }

                                SFText {
                                    Layout.fillWidth: true
                                    //% "<style>a:link {color: '#00f6d2'}</style>2. Send email to <a href='mailto:support@beam.mw'>support@beam.mw</a> or open a ticket in <a href='https://github.com/beam-mw/beam'>github</a>"
                                    text: qsTrId("settings-report-problem-message-line-2")
                                    color: Style.content_main
                                    textFormat: Text.RichText
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                    onLinkActivated: Qt.openUrlExternally(link)

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.NoButton
                                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    }
                                }

                                SFText {
                                    Layout.fillWidth: true
                                    //% "3. Don't forget to attach logs archive"
                                    text: qsTrId("settings-report-problem-message-line-3")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }
                            }                        
                            RowLayout {
                                Layout.fillWidth: true
                                CustomButton {
                                    //% "save wallet logs"
                                    text: qsTrId("settings-report-problem-save-log-button")
                                    palette.buttonText : "white"
                                    palette.button: Style.background_button
                                    onClicked: viewModel.reportProblem()
                                }
                                spacing: 30
                                CustomButton {
                                    icon.source: "qrc:/assets/icon-restore.svg"
                                    Layout.alignment: Qt.AlignRight
                                    //% "rescan"
                                    text: qsTrId("settings-rescan-button")
                                    palette.button: Style.background_button
                                    palette.buttonText : localNodeRun.checked ? Style.content_main : Style.content_disabled
                                    enabled: localNodeRun.checked && confirmRefreshDialog.canRefresh && viewModel.isLocalNodeRunning
                                    onClicked: {
                                        confirmRefreshDialog.open();
                                    }
                                }
                            }

                            SFText {
                                Layout.topMargin: 20
                                Layout.fillWidth: true
                                Layout.minimumHeight: 20
                                //% "Wallet folder location:"
                                text: qsTrId("settings-wallet-location-label")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                            }

                            SFTextInput {
                                Layout.fillWidth: true
                                
                                font.pixelSize: 14
                                color: Style.content_disabled
                                readOnly: true
                                activeFocusOnTab: false
                                text: viewModel.walletLocation
                            }
                            CustomButton {
                                //% "copy"
                                text: qsTrId("settings-wallet-location-copy-button")
                                icon.color: Style.content_main
                                palette.buttonText : Style.content_main
                                palette.button: Style.background_button
                                icon.source: "qrc:/assets/icon-copy.svg"
                                onClicked: {
                                    viewModel.copyToClipboard(viewModel.walletLocation);
                                }
                            }
                            Item {
                                Layout.fillHeight: true;
                            }
                        }

                    }

                
                }

                Item {
                    Layout.fillHeight: true
                }
            }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 30
            PrimaryButton {
                //% "undo changes"
                text: qsTrId("settings-undo")
                enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                }
                onClicked: viewModel.undoChanges()
            }

            PrimaryButton {      
                //% "apply changes"
                text: qsTrId("settings-apply")  
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
