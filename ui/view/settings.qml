import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "controls"
import Beam.Wallet 1.0

Rectangle {

    anchors.fill: parent
    color: "#032e48"

    SettingsViewModel {id: viewModel}

    ConfirmationDialog {
        id: emergencyConfirmation
        text: "Transaction history will be deleted. This operation can not be undone"
        okButtonText: "reset"
        onAccepted: viewModel.emergencyReset()
    }

    ChangePasswordDialog {
        id: changePasswordDialog

        
    }

    ColumnLayout {
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
                color: Style.white
                text: "Settings"
            }

            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom | Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 14
                color: Style.white
                text: "Version: " + viewModel.version
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    height: 150

                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 5

                        SFText {
                            Layout.alignment: Qt.AlignTop
                            Layout.bottomMargin: 15
                            text: qsTr("Remote node")
                            color: Style.white
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFText {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("ip:port")
                            color: viewModel.localNodeRun ? Style.disable_text_color : Style.white
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
                            color: readOnly ? Style.disable_text_color : Style.white
                            readOnly: viewModel.localNodeRun
                            validator: RegExpValidator { regExp: /^(\s|\x180E)*(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
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
                                color: Style.validator_color
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
                    height: 360
                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        enabled: viewModel.localNodeRun

                        SFText {
                            text: qsTr("Local node")
                            color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
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
                                

                              /*  CustomSwitch {
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
                        */

                                SFText {
                                    text: qsTr("Local node port")
                                    color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }

                                SFTextInput {
                                    id: localNodePort
                                    width: parent.width
                                    activeFocusOnTab: true
                                    font.pixelSize: 12
                                    color: readOnly ? Style.disable_text_color : Style.white
                                    readOnly: !viewModel.localNodeRun
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

                                SFText {
                                    text: qsTr("Mining threads")
                                    color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }

                                FeeSlider {
                                    id: localNodeMiningThreads
                                    precision: 0
                                    showTicks: true
                                    Layout.fillWidth: true
                                    value: viewModel.localNodeMiningThreads
                                    to: {viewModel.coreAmount()}
                                    stepSize: 1
                                    enabled: viewModel.localNodeRun
                                    Binding {
                                        target: viewModel
                                        property: "localNodeMiningThreads"
                                        value: localNodeMiningThreads.value
                                    }
                                }

                                SFText {
                                    text: qsTr("Verification threads")
                                    color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
                                    font.pixelSize: 12
                                    font.styleName: "Bold"; font.weight: Font.Bold
                                }

                                FeeSlider {
                                    id: localNodeVerificationThreads
                                    precision: 0
                                    showTicks: true
                                    Layout.fillWidth: true
                                    value: viewModel.localNodeVerificationThreads
                                    to: {viewModel.coreAmount()}
                                    stepSize: 1
                                    enabled: viewModel.localNodeRun
                                    Binding {
                                        target: viewModel
                                        property: "localNodeVerificationThreads"
                                        value: localNodeVerificationThreads.value
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 10

                                SFText {
                                    text: qsTr("Peers")
                                    color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
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
                                        color: readOnly ? Style.disable_text_color : Style.white
                                        readOnly: !viewModel.localNodeRun
                                        validator: RegExpValidator { regExp: /^(\s|\x180E)*(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                                    }
                                    
                                    PrimaryButton {
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 20
                                        Layout.minimumWidth: 60
				                        text: "Add"
                                        palette.button: "#708090"
                                        palette.buttonText : viewModel.localNodeRun ? Style.white : Style.disable_text_color
                                        enabled: newLocalNodePeer.acceptableInput && viewModel.localNodeRun
                                        onClicked: {
                                            viewModel.addLocalNodePeer(newLocalNodePeer.text.trim());
                                            newLocalNodePeer.clear();
                                        }
                                    }
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumWidth: 140
                                    model: viewModel.localNodePeers
                                    clip: true
                                    delegate: RowLayout {
                                        Layout.fillWidth: true
                                        height: 30
                                        SFText {
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignVCenter
                                            text: modelData
                                            font.pixelSize: 12
                                            color: viewModel.localNodeRun ? Style.white : Style.disable_text_color
                                            height: 16
                                            elide: Text.ElideRight
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                        CustomButton {
                                            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                            Layout.minimumHeight: 20
                                            Layout.minimumWidth: 20
                                            textOpacity: 0
                                            icon.source: "qrc:///assets/icon-delete.svg"
                                            enabled: viewModel.localNodeRun
                                            onClicked: viewModel.deleteLocalNodePeer(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    height: 150

                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10

                        SFText {
                            text: qsTr("Reset wallet data")
                            color: Style.white
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFText {
                            Layout.fillWidth: true
                            text: qsTr("Clear all local data and retrieve most updated information from blockchain. Transaction history will be deleted.")
                            color: Style.white
                            font.pixelSize: 12
                            font.styleName: "Bold"; font.weight: Font.Bold
                            wrapMode: Text.WordWrap
                        }

                        PrimaryButton {
                            Layout.minimumHeight: 38
                            Layout.minimumWidth: 198
                            text: "reset local data"
                            palette.button: "#708090"
                            palette.buttonText : "white"
                            icon.source: "qrc:///assets/icon-reset.svg"
                            icon.width: 16
                            icon.height: 16
                            onClicked: emergencyConfirmation.open();
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 210
                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10

                        SFText {
                            text: qsTr("Report problem")
                            color: Style.white
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        ColumnLayout {
                            SFText {
                                Layout.fillWidth: true
                                text: qsTr("To report a problem:")
                                color: Style.white
                                font.pixelSize: 12
                                font.styleName: "Bold"; font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                            }

                            SFText {
                                Layout.fillWidth: true
                                text: qsTr("1. Click 'Save wallet logs' and choose a destination folder for log archive")
                                color: Style.white
                                font.pixelSize: 12
                                font.styleName: "Bold"; font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                            }

                            SFText {
                                Layout.fillWidth: true
                                text: qsTr("<style>a:link {color: '#00f6d2'}</style>2. Send email to <a href='mailto:testnet@beam-mw.com'>testnet@beam-mw.com</a> or open a ticket in <a href='https://github.com/beam-mw/beam'>github</a>")
                                color: Style.white
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
                                color: Style.white
                                font.pixelSize: 12
                                font.styleName: "Bold"; font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                            }
                        }                        

                        CustomButton {
                            Layout.minimumHeight: 38
                            Layout.minimumWidth: 150

                            text: "save wallet logs"
                            palette.buttonText : "white"
                            palette.button: "#708090"
                            onClicked: viewModel.reportProblem()
                        }
                    }

                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: Style.dark_slate_blue
                    height: childrenRect.height + 40

                    Column {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 20
                        spacing: 10

                        SFText {
                            text: qsTr("General settings")
                            color: Style.white
                            font.pixelSize: 18
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        Row {
                            width: parent.width

                            spacing: 10

                            SFText {
                                text: qsTr("Lock screen in")
                                color: Style.white
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


                        CustomButton {
                            width: 244

                            text: "change wallet password"
                            palette.buttonText : "white"
                            palette.button: "#708090"
                            icon.source: "qrc:///assets/icon-password.svg"
                            icon.width: 16
                            icon.height: 16
                            onClicked: changePasswordDialog.open()
                        }
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
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
                    /*&& localNodeMiningThreads.acceptableInput
                    && localNodeVerificationThreads.acceptableInput*/}
                onClicked: viewModel.undoChanges()
            }

            PrimaryButton {        
                text: qsTr("apply changes")
                enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                    /*&& localNodeMiningThreads.acceptableInput
                    && localNodeVerificationThreads.acceptableInput*/}
                onClicked: viewModel.applyChanges()
            }
        }
    }
}
