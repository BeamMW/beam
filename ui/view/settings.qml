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
                text: "Settings"
            }

            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom | Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 14
                color: Style.content_main
                text: "Version: " + viewModel.version
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
                                text: qsTr("Remote node")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            SFText {
                                Layout.alignment: Qt.AlignTop
                                text: qsTr("ip:port")
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
                                validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
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
                                    visible: !nodeAddress.acceptableInput
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
                                text: qsTr("Local node")
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
                                        text: qsTr("Run local node")
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
                                        text: qsTr("Local node port")
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
                                            bottom: 0
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
                                        text: qsTr("Peers")
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
                                            validator: RegExpValidator { regExp: /^(\s|\x180E)*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])|([\w.-]+(?:\.[\w\.-]+)+))(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                        }
                                    
                                        CustomButton {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 70
                                            leftPadding: 20
                                            rightPadding: 20
                                            text: "Add"
                                            palette.button: Style.background_inconspicuous
                                            palette.buttonText : localNodeRun.checked ? Style.content_main : Style.content_disabled
                                            enabled: newLocalNodePeer.acceptableInput && localNodeRun.checked
                                            onClicked: {
                                                viewModel.addLocalNodePeer(newLocalNodePeer.text.trim());
                                                newLocalNodePeer.clear();
                                            }
                                        }
                                    }

                                    SFText {
                                        text: qsTr("Please add at least one peer")
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
                    okButtonText: qsTr("rescan")
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
                                text: qsTr("Rescan")
                            }
                            SFText {
                                width: parent.width
                                leftPadding: 20
                                rightPadding: 20
                                font.pixelSize: 14
                                color: Style.content_main
                                wrapMode: Text.Wrap
                                horizontalAlignment : Text.AlignHCenter
                                text: qsTr("Rescan will sync transaction and UTXO data with the latest information on the blockchain. The process might take long time. \n\nAre you sure?")
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

                            SFText {
                                text: qsTr("General settings")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            Row {
                                width: parent.width

                                spacing: 10

                                SFText {
                                    text: qsTr("Lock screen in")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                }

                                CustomComboBox {
                                    id: lockTimeoutControl
                                    width: 100
                                    height: 20
                                    anchors.top: parent.top
                                    anchors.topMargin: -3

                                    currentIndex: viewModel.lockTimeout

                                    Binding {
                                        target: viewModel
                                        property: "lockTimeout"
                                        value: lockTimeoutControl.currentIndex
                                    }

                                    model: ["never", "1 minute", "5 minutes", "15 minutes", "30 minutes", "1 hour"]
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 10

                                CustomSwitch {
                                    id: isPasswordReqiredToSpendMoney
                                    text: qsTr("Ask password for every sending transaction")
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
                                text: "change wallet password"
                                palette.buttonText : "white"
                                palette.button: Style.background_inconspicuous
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
                                text: qsTr("Report problem")
                                color: Style.content_main
                                font.pixelSize: 18
                                font.styleName: "Bold"; font.weight: Font.Bold
                            }

                            ColumnLayout {
                                SFText {
                                    Layout.fillWidth: true
                                    text: qsTr("To report a problem:")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }

                                SFText {
                                    Layout.fillWidth: true
                                    text: qsTr("1. Click 'Save wallet logs' and choose a destination folder for log archive")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }

                                SFText {
                                    Layout.fillWidth: true
                                    text: qsTr("<style>a:link {color: '#00f6d2'}</style>2. Send email to <a href='mailto:support@beam.mw'>support@beam.mw</a> or open a ticket in <a href='https://github.com/beam-mw/beam'>github</a>")
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
                                    text: qsTr("3. Don't forget to attach logs archive")
                                    color: Style.content_main
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                    wrapMode: Text.WordWrap
                                }
                            }                        
                            RowLayout {
                                Layout.fillWidth: true
                                CustomButton {
                                    text: "save wallet logs"
                                    palette.buttonText : "white"
                                    palette.button: Style.background_inconspicuous
                                    onClicked: viewModel.reportProblem()
                                }
                                spacing: 30
                                CustomButton {
                                    icon.source: "qrc:/assets/icon-restore.svg"
                                    Layout.alignment: Qt.AlignRight
                                    text: qsTr("rescan")
                                    palette.button: Style.background_inconspicuous
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
                                text: qsTr("Wallet folder location:")
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
                                text: qsTr("copy")
                                icon.color: Style.content_main
                                palette.buttonText : Style.content_main
                                palette.button: Style.background_inconspicuous
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
                text: qsTr("undo changes")
                enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                }
                onClicked: viewModel.undoChanges()
            }

            PrimaryButton {        
                text: qsTr("apply changes")
                enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                    && (localNodeRun.checked ? (viewModel.localNodePeers.length > 0) : true)
                }
                onClicked: viewModel.applyChanges()
            }
        }
    }
}
