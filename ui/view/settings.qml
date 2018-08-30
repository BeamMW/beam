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
        text: "Do you really want to reset your wallet?"
        okButtonText: "reset"
        onAccepted: viewModel.emergencyReset()
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
                    height: 160

                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 5

                        SFText {
                            Layout.alignment: Qt.AlignTop
                            Layout.bottomMargin: 15
                            text: qsTr("Connect to node")
                            color: Style.white
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }

                        SFText {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("ip:port")
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        SFTextInput {
                            id: nodeAddress
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            focus: true
                            activeFocusOnTab: true
                            font.pixelSize: 12
                            color: readOnly ? Style.disable_text_color : Style.white
                            readOnly: localNodeRun.checked
                            validator: RegExpValidator { regExp: /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?$/ }
                            text: viewModel.nodeAddress
                            Binding {
                                target: viewModel
                                property: "nodeAddress"
                                value: nodeAddress.text
                            }
                        }

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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    height: 320
                    radius: 10
                    color: Style.dark_slate_blue

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10

                        SFText {
                            text: qsTr("Local node")
                            color: Style.white
                            font.pixelSize: 18
                            font.weight: Font.Bold
                        }

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
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        SFTextInput {
                            id: localNodePort
                            width: parent.width
                            activeFocusOnTab: true
                            font.pixelSize: 12
                            color: readOnly ? Style.disable_text_color : Style.white
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

                        SFText {
                            text: qsTr("Mining threads")
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        SFTextInput {
                            id: localNodeMiningThreads
                            width: parent.width
                            activeFocusOnTab: true
                            font.pixelSize: 12
                            color: readOnly ? Style.disable_text_color : Style.white
                            readOnly: !localNodeRun.checked
                            text: viewModel.localNodeMiningThreads
                            validator: IntValidator {
                                bottom: 0
                                top: viewModel.coreAmount()
                            }
                            Binding {
                                target: viewModel
                                property: "localNodeMiningThreads"
                                value: localNodeMiningThreads.text
                            }
                        }

                        SFText {
                            text: qsTr("Verification threads")
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        SFTextInput {
                            id: localNodeVerificationThreads
                            width: parent.width
                            activeFocusOnTab: true
                            font.pixelSize: 12
                            color: readOnly ? Style.disable_text_color : Style.white
                            readOnly: !localNodeRun.checked
                            text: viewModel.localNodeVerificationThreads
                            validator: IntValidator {
                                bottom: 0
                                top: viewModel.coreAmount()
                            }
                            Binding {
                                target: viewModel
                                property: "localNodeVerificationThreads"
                                value: localNodeVerificationThreads.text
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
                    height: 160

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
                            font.weight: Font.Bold
                        }

                        SFText {
                            text: qsTr("Add explanation to the reset sections!")
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        PrimaryButton {
                            Layout.minimumHeight: 38
                            Layout.minimumWidth: 198
                            text: "emergency reset"
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
                    height: 160
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
                            font.weight: Font.Bold
                        }

                        SFText {
                            text: qsTr("Add explanation to the report sections!")
                            color: Style.white
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        CustomButton {
                            Layout.minimumHeight: 38
                            Layout.minimumWidth: 150

                            text: "report problem"
                            palette.buttonText : "white"
                            palette.button: "#708090"
                            onClicked: viewModel.reportProblem()
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

            CustomButton {
                text: qsTr("undo changes")
                onClicked: viewModel.undoChanges()
            }

            PrimaryButton {        
                text: qsTr("apply changes")
                enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                    && localNodeMiningThreads.acceptableInput
                    && localNodeVerificationThreads.acceptableInput}
                onClicked: viewModel.applyChanges()
            }
        }
    }
}
