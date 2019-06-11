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

    OpenExternalLinkConfirmation {
        id: externalLinkConfirmation
    }

    ChangePasswordDialog {
        id: changePasswordDialog        
    }

    ConfirmationDialog {
        id: confirmRefreshDialog
        property bool canRefresh: true
        //: settings tab, confirm rescan dialog, rescan button
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
                    //: settings tab, confirm rescan dialog, title
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

    function handleMousePointer(mouse, element) {
        if (element.parent.linkAt(mouse.x, mouse.y).length) {
            element.cursorShape = Qt.PointingHandCursor;
        } else {
            element.cursorShape = Qt.ArrowCursor;
        }
    }

    function handleExternalLink(mouse, element) {
        if (element.cursorShape == Qt.PointingHandCursor) {
            var externalLink = element.parent.linkAt(mouse.x, mouse.y);
            if (viewModel.isAllowedBeamMWLinks) {
                Qt.openUrlExternally(externalLink);
            } else {
                externalLinkConfirmation.externalUrl = externalLink;
                externalLinkConfirmation.onOkClicked = function () {
                    viewModel.isAllowedBeamMWLinks = true;
                };
                externalLinkConfirmation.open();
            }
        } else {
            viewModel.isAllowedBeamMWLinks = !viewModel.isAllowedBeamMWLinks;
        }
    }

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        spacing: 20
        anchors.bottomMargin: 30

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40

            SFText {
                Layout.alignment: Qt.AlignBottom | Qt.AlignLeft
                font.pixelSize: 36
                color: Style.content_main
                //: settings tab title
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
                //% "Version: "
                text: qsTrId("settings-version") + viewModel.version
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 14          
            
            StatusBar {
                Layout.preferredHeight: 14
                id: status_bar
                model: statusbarModel
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                RowLayout {
                    width: mainColumn.width
                    spacing: 10

                    ColumnLayout {
                        Layout.preferredWidth: mainColumn.width * 0.4
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
                                        //: settings tab, node section, on address error
                                        //% "Invalid address"
                                        text: qsTrId("settings-remote-node-ip-port-error")
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
                                        //: settings tab, node section, cancel button
                                        //% "cancel"
                                        text: qsTrId("settings-undo")
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
                                        //% "apply"
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
                        Layout.preferredWidth: mainColumn.width * 0.6
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
                                        //: settings tab, general section, show data folder link
                                        //% "show in folder"
                                        text: qsTrId("settings-wallet-location-link")
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
                                        //: settings tab, general section, ask password label
                                        //% "Ask password for every sending transaction"
                                        text: qsTrId("settings-general-require-pwd-to-spend")
                                        font.pixelSize: 14
                                        Layout.fillWidth: true
                                        checked: viewModel.isPasswordReqiredToSpendMoney
                                        Binding {
                                            target: viewModel
                                            property: "isPasswordReqiredToSpendMoney"
                                            value: isPasswordReqiredToSpendMoney.checked
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.preferredHeight: 32

                                    SFText {
                                        //: general settings, label for alow open external links
                                        //% "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>Allow access to <a href='https://www.beam.mw/'>beam.mw</a> and <a href='https://explorer.beam.mw/'>blockchain explorer</a> (to fetch exchanges and transaction data)"
                                        text: qsTrId("settings-general-allow-beammw-label")
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
                                                handleExternalLink(mouse, allowOpenExternalArea);
                                            }
                                            hoverEnabled: true
                                            onPositionChanged : {
                                                handleMousePointer(mouse, allowOpenExternalArea);
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
                                //: settings tab, change password button
                                //% "change wallet password"
                                text: qsTrId("settings-general-change-pwd-button")
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
                                //: settings tab, rescan button                            
                                //% "rescan"
                                text: qsTrId("settings-rescan-button")
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
                                    Layout.topMargin: 7
                                    Layout.preferredWidth: 419
                                    //: settings tab, report problem section, message
                                    //% "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>To report a problem:<br />1. Click “Save wallet logs” and choose a destination folder for log archive<br />2. Send email to <a href='mailto:support@beam.mw'>support@beam.mw</a> or open a ticket in <a href='https://github.com/BeamMW'>Github</a><br />3. Don’t forget to attach logs archive"
                                    text: qsTrId("settings-report-problem-message")
                                    textFormat: Text.RichText
                                    color: Style.content_main
                                    font.pixelSize: 14
                                    wrapMode: Text.WordWrap
                                    MouseArea {
                                        id: reportProblemMessageArea
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        onClicked: {
                                            handleExternalLink(mouse, reportProblemMessageArea);
                                        }
                                        hoverEnabled: true
                                        onPositionChanged : {
                                            handleMousePointer(mouse, reportProblemMessageArea);
                                        }
                                    }
                                }

                                CustomButton {
                                    Layout.topMargin: 10
                                    Layout.preferredHeight: 38
                                    Layout.preferredWidth: 191
                                    Layout.alignment: Qt.AlignLeft
                                    //: settings tab, report problem section, save logs button
                                    //% "save wallet logs"
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

                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
            }
        }
    }
}
